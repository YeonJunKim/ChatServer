#include <iostream>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>

using namespace boost;
using std::cout;
using std::endl;
using std::string;

struct Session
{
	shared_ptr<asio::ip::tcp::socket> sock;
	asio::ip::tcp::endpoint ep;
	string id;
	int room_no = -1;

	string sbuf;
	string rbuf;
	char buf[80];
};


class Server
{
	asio::io_service ios;
	shared_ptr<asio::io_service::work> work;
	asio::ip::tcp::endpoint ep;
	asio::ip::tcp::acceptor gate;
	std::vector<Session*> sessions;
	boost::thread_group threadGroup;
	boost::mutex lock;
	std::vector<int> existingRooms;
	const int THREAD_SIZE = 4;

	enum Code {INVALID, SET_ID, CREATE_ROOM, SET_ROOM, WHISPER_TO, KICK_ID};

public:
	Server(string ip_address, unsigned short port_num) :
		work(new asio::io_service::work(ios)),
		ep(asio::ip::address::from_string(ip_address), port_num),
		gate(ios, ep.protocol())
	{
		existingRooms.push_back(0);
	}


	void Start()
	{
		cout << "Start Server" << endl;
		cout << "Creating Threads" << endl;
		for (int i = 0; i < THREAD_SIZE; i++)
			threadGroup.create_thread(bind(&Server::WorkerThread, this));

		// thread �� ������������� ��� ��ٸ��� �κ�
		this_thread::sleep_for(chrono::milliseconds(100));
		cout << "Threads Created" << endl;

		ios.post(bind(&Server::OpenGate, this));

		threadGroup.join_all();
	}

private:
	void WorkerThread()
	{
		lock.lock();
		cout << "[" << boost::this_thread::get_id() << "]" << " Thread Start" << endl;
		lock.unlock();

		ios.run();

		lock.lock();
		cout << "[" << boost::this_thread::get_id() << "]" << " Thread End" << endl;
		lock.unlock();
	}

	void OpenGate()
	{
		system::error_code ec;
		gate.bind(ep, ec);
		if (ec)
		{
			cout << "bind failed: " << ec.message() << endl;
			return;
		}

		gate.listen();
		cout << "Gate Opened" << endl;

		StartAccept();
		cout << "[" << boost::this_thread::get_id() << "]" << " Start Accepting" << endl;
	}

	// �񵿱�� Accept
	void StartAccept()
	{
		Session* session = new Session();
		shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(ios));
		session->sock = sock;
		gate.async_accept(*sock, session->ep, bind(&Server::OnAccept, this, _1, session));
	}

	void OnAccept(const system::error_code &ec, Session* session)
	{
		if (ec)
		{
			cout << "accept failed: " << ec.message() << endl;
			return;
		}

		lock.lock();
		sessions.push_back(session);
		cout << "[" << boost::this_thread::get_id() << "]" << " Client Accepted" << endl;
		lock.unlock();

		ios.post(bind(&Server::Receive, this, session));
		StartAccept();
	}

	// ����� Receive (�����尡 ������ ������ 1:1 ���)
	void Receive(Session* session)
	{
		system::error_code ec;
		size_t size;
		size = session->sock->read_some(asio::buffer(session->buf, sizeof(session->buf)), ec);

		if (ec)
		{
			cout << "[" << boost::this_thread::get_id() << "] read failed: " << ec.message() << endl;
			CloseSession(session);
			return;
		}

		if (size == 0)
		{
			cout << "[" << boost::this_thread::get_id() << "] peer wants to end " << endl;
			CloseSession(session);
			return;
		}

		session->buf[size] = '\0';
		session->rbuf = session->buf;
		PacketManager(session);
		cout << "[" << boost::this_thread::get_id() << "] " << session->rbuf << endl;

		Receive(session);
	}

	void PacketManager(Session* session)
	{
		// :~ ��� Ư��(?)�޼����� �������� ��� ó��
		if (session->buf[0] == ':')
		{
			Code code = TranslatePacket(session->rbuf);

			switch (code)
			{
			case Code::SET_ID:
				SetID(session);
				break;
			case Code::CREATE_ROOM:
				CreateRoom(session);
				break;
			case Code::SET_ROOM:
				SetRoom(session);
				break;
			case Code::WHISPER_TO:
				WhisperTo(session);
				break;
			case Code::KICK_ID:
				// �̱���
				break;
			case Code::INVALID:
				session->sbuf = "��ȿ���� ���� ��ɾ� �Դϴ�";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				break;
			}
		}
		else  // :~ ��� Ư���޼����� �ƴϰ� �׳� ä���� ���
		{
			if (session->id.length() != 0) // id length�� 0�� ���� id�� ���� ������� ���� ���
			{
				string temp = "[" + session->id + "]:" + session->rbuf;
				SendAll(session, session->room_no, temp, false);
			}
			else
			{
				session->sbuf = ":set �� ���� ���̵� ���� ����ϼ���";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			}
		}
	}

	Code TranslatePacket(string message)
	{
		string temp = message.substr(0, sizeof(":set ") - 1);
		// :set �� ���
		if (temp.compare(":set ") == 0)
		{
			return Code::SET_ID;
		}

		temp = message.substr(0, sizeof(":createRoom ") - 1);
		if (temp.compare(":createRoom ") == 0)
		{
			return Code::CREATE_ROOM;
		}

		temp = message.substr(0, sizeof(":setRoom ") - 1);
		if (temp.compare(":setRoom ") == 0)
		{
			return Code::SET_ROOM;
		}

		temp = message.substr(0, sizeof(":to ") - 1);
		if (temp.compare(":to ") == 0)
		{
			return Code::WHISPER_TO;
		}

		temp = message.substr(0, sizeof(":kick ") - 1);
		if (temp.compare(":kick ") == 0)
		{
			return Code::KICK_ID;
		}

		return Code::INVALID;
	}


	void SetID(Session* session)
	{
		string temp = session->rbuf.substr(sizeof(":set ") - 1, session->rbuf.length());
		// �ߺ��� ���̵����� üũ
		for (int i = 0; i < sessions.size(); i++)
		{
			if (temp.compare(sessions[i]->id) == 0)
			{
				session->sbuf = "set falied: [" + temp + "]�� �̹� ������� ���̵� �Դϴ�";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				return;
			}
		}

		session->id = temp;
		session->sbuf = "set [" + temp + "] success!";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));

		if (session->room_no == -1)
		{
			session->room_no = 0;
			SendAll(session, 0, "[" + session->id + "] ���� �κ� �����Ͽ����ϴ�", false);
		}
		else
		{
			SendAll(session, session->room_no, "[" + session->id + "] ���� ���̵� �����Ͽ����ϴ�", false);
		}
	}

	void CreateRoom(Session* session)
	{
		if (session->room_no != 0)
		{
			session->sbuf = "creatRoom falied: ���� �κ񿡼��� ������ �� �ֽ��ϴ�";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":createRoom ") - 1, session->rbuf.length());
		// �޼����� ���ȣ �κ��� ������ �´��� üũ
		if (IsTheMessageInNumbers(temp) == false)
		{
			session->sbuf = "creatRoom falied: [" + temp + "]�� ��ȿ���� �ʴ� ���Դϴ�";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		int num = atoi(temp.c_str());
		// �̹� �����ϴ� ������ üũ
		for (int i = 0; i < existingRooms.size(); i++)
		{
			if (existingRooms[i] == num)
			{
				session->sbuf = "creatRoom falied: [" + temp + "]�� ���� �̹� �����մϴ�";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				return;
			}
		}

		lock.lock();
		existingRooms.push_back(num);
		lock.unlock();

		session->room_no = num;
		session->sbuf = "createRoom [" + temp + "] success!";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));

		SendAll(session, 0, "[" + temp + "]�� ���� �����Ǿ����ϴ�", false);

		session->sbuf = "[" + temp + "]�� �濡 �����Ͽ����ϴ�";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void SetRoom(Session* session)
	{
		if (session->id.length() == 0)
		{
			session->sbuf = ":set �� ���� ���̵� ���� ����ϼ���";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":setRoom ") - 1, session->rbuf.length());
		// �޼����� ���ȣ �κ��� ������ �´��� üũ
		if (IsTheMessageInNumbers(temp) == false)
		{
			session->sbuf = "setRoom falied: [" + temp + "]�� ��ȿ���� �ʴ� ���Դϴ�";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		int num = atoi(temp.c_str());
		if (session->room_no == num)
		{
			session->sbuf = "setRoom falied: �̹� [" + temp + "]�� �濡 �ֽ��ϴ�";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}
	
		// �����ϴ� ������ üũ
		for (int i = 0; i < existingRooms.size(); i++)
		{
			if (existingRooms[i] == num)
			{
				SendAll(session, session->room_no, "[" + session->id + "] ���� ���� �������ϴ�", false);
				session->room_no = num;

				if(num == 0)
					session->sbuf = "�κ�� �̵��Ͽ����ϴ�";
				else
					session->sbuf = "[" + temp + "]�� ������ �̵��Ͽ����ϴ�";

				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				return;
			}
		}

		session->sbuf = "setRoom falied: [" + temp + "]�� ���� �������� �ʽ��ϴ�";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void WhisperTo(Session* session)
	{
		if (session->id.length() == 0)
		{
			session->sbuf = ":set �� ���� ���̵� ���� ����ϼ���";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":to ") - 1, session->rbuf.length());
		int num = 0;
		num	= temp.find_first_of(' ');
		if (num == 0)
		{
			session->sbuf = "���̵�� �޼��� ���� ������⸦ ���ּ���";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp2 = temp.substr(0, num);
		for (int i = 0; i < sessions.size(); i++)
		{
			if (sessions[i]->id.compare(temp2) == 0)
			{
				sessions[i]->sbuf = "from [" + session->id + "]:" + temp.substr(num+1, temp.length());
				sessions[i]->sock->async_write_some(asio::buffer(sessions[i]->sbuf), 
					bind(&Server::OnSend, this, _1));
				return;
			}
		}

		session->sbuf = "���̵� ã�� �� �����ϴ�";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void SendAll(Session* session, int room_no, string message, bool sendToSenderAsWell)
	{
		// ���� �濡 �ִ� �ٸ� ��� Ŭ���̾�Ʈ�鿡�� ������
		for (int i = 0; i < sessions.size(); i++)
		{
			if ((session->sock != sessions[i]->sock) && (room_no == sessions[i]->room_no))
			{
				sessions[i]->sbuf = message;
				sessions[i]->sock->async_write_some(asio::buffer(sessions[i]->sbuf), 
					bind(&Server::OnSend, this, _1));
			}
		}

		// �޼����� ������ Ŭ���̾�Ʈ���Ե� ������
		if (sendToSenderAsWell)
		{
			session->sbuf = message;
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
		}
	}

	void OnSend(const system::error_code &ec)
	{
		if (ec)
		{
			cout << "[" << boost::this_thread::get_id() << "] async_write_some failed: " << ec.message() << endl;
			return;
		}
	}

	bool IsTheMessageInNumbers(string message)
	{
		const char* cTemp = message.c_str();

		// �޼��� ����(���ȣ)�� ������ �ƴ� ���
		for (int i = 0; i < message.length(); i++)
		{
			if (cTemp[i] < '0' || cTemp[i] > '9')
			{
				return false;
			}
		}

		return true;
	}


	void CloseSession(Session* session)
	{
		if (session->room_no != -1)
		{
			SendAll(session, 0, "[" + session->id + "]" + "���� �����Ͽ����ϴ�", false);
			SendAll(session, session->room_no, "[" + session->id + "]" + "���� ���� �������ϴ�", false);
		}

		// if session ends, close and erase
		for (int i = 0; i < sessions.size(); i++)
		{
			if (sessions[i]->sock == session->sock)
			{
				lock.lock();
				sessions.erase(sessions.begin() + i);
				lock.unlock();
				break;
			}
		}

		string temp = session->id;
		session->sock->close();
		delete session;
	}
};


int main()
{
	Server serv(asio::ip::address_v4::any().to_string(), 3333);
	serv.Start();

	return 0;
}