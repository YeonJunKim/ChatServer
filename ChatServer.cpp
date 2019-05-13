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

		// thread 잘 만들어질때까지 잠시 기다리는 부분
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

	// 비동기식 Accept
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

	// 동기식 Receive (쓰레드가 각각의 세션을 1:1 담당)
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
		// :~ 라는 특수(?)메세지를 보내왔을 경우 처리
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
				// 미구현
				break;
			case Code::INVALID:
				session->sbuf = "유효하지 않은 명령어 입니다";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				break;
			}
		}
		else  // :~ 라는 특수메세지가 아니고 그냥 채팅일 경우
		{
			if (session->id.length() != 0) // id length가 0인 경우는 id를 아직 등록하지 않은 경우
			{
				string temp = "[" + session->id + "]:" + session->rbuf;
				SendAll(session, session->room_no, temp, false);
			}
			else
			{
				session->sbuf = ":set 을 통해 아이디를 먼저 등록하세요";
				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			}
		}
	}

	Code TranslatePacket(string message)
	{
		string temp = message.substr(0, sizeof(":set ") - 1);
		// :set 일 경우
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
		// 중복된 아이디인지 체크
		for (int i = 0; i < sessions.size(); i++)
		{
			if (temp.compare(sessions[i]->id) == 0)
			{
				session->sbuf = "set falied: [" + temp + "]는 이미 사용중인 아이디 입니다";
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
			SendAll(session, 0, "[" + session->id + "] 님이 로비에 입장하였습니다", false);
		}
		else
		{
			SendAll(session, session->room_no, "[" + session->id + "] 님이 아이디를 변경하였습니다", false);
		}
	}

	void CreateRoom(Session* session)
	{
		if (session->room_no != 0)
		{
			session->sbuf = "creatRoom falied: 방은 로비에서만 생성할 수 있습니다";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":createRoom ") - 1, session->rbuf.length());
		// 메세지의 방번호 부분이 정수가 맞는지 체크
		if (IsTheMessageInNumbers(temp) == false)
		{
			session->sbuf = "creatRoom falied: [" + temp + "]는 유효하지 않는 값입니다";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		int num = atoi(temp.c_str());
		// 이미 존재하는 방인지 체크
		for (int i = 0; i < existingRooms.size(); i++)
		{
			if (existingRooms[i] == num)
			{
				session->sbuf = "creatRoom falied: [" + temp + "]번 방은 이미 존재합니다";
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

		SendAll(session, 0, "[" + temp + "]번 방이 생성되었습니다", false);

		session->sbuf = "[" + temp + "]번 방에 입장하였습니다";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void SetRoom(Session* session)
	{
		if (session->id.length() == 0)
		{
			session->sbuf = ":set 을 통해 아이디를 먼저 등록하세요";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":setRoom ") - 1, session->rbuf.length());
		// 메세지의 방번호 부분이 정수가 맞는지 체크
		if (IsTheMessageInNumbers(temp) == false)
		{
			session->sbuf = "setRoom falied: [" + temp + "]는 유효하지 않는 값입니다";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		int num = atoi(temp.c_str());
		if (session->room_no == num)
		{
			session->sbuf = "setRoom falied: 이미 [" + temp + "]번 방에 있습니다";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}
	
		// 존재하는 방인지 체크
		for (int i = 0; i < existingRooms.size(); i++)
		{
			if (existingRooms[i] == num)
			{
				SendAll(session, session->room_no, "[" + session->id + "] 님이 방을 나갔습니다", false);
				session->room_no = num;

				if(num == 0)
					session->sbuf = "로비로 이동하였습니다";
				else
					session->sbuf = "[" + temp + "]번 방으로 이동하였습니다";

				session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
				return;
			}
		}

		session->sbuf = "setRoom falied: [" + temp + "]번 방이 존재하지 않습니다";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void WhisperTo(Session* session)
	{
		if (session->id.length() == 0)
		{
			session->sbuf = ":set 을 통해 아이디를 먼저 등록하세요";
			session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
			return;
		}

		string temp = session->rbuf.substr(sizeof(":to ") - 1, session->rbuf.length());
		int num = 0;
		num	= temp.find_first_of(' ');
		if (num == 0)
		{
			session->sbuf = "아이디와 메세지 사이 띄워쓰기를 해주세요";
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

		session->sbuf = "아이디를 찾을 수 없습니다";
		session->sock->async_write_some(asio::buffer(session->sbuf), bind(&Server::OnSend, this, _1));
	}

	void SendAll(Session* session, int room_no, string message, bool sendToSenderAsWell)
	{
		// 같은 방에 있는 다른 모든 클라이언트들에게 보낸다
		for (int i = 0; i < sessions.size(); i++)
		{
			if ((session->sock != sessions[i]->sock) && (room_no == sessions[i]->room_no))
			{
				sessions[i]->sbuf = message;
				sessions[i]->sock->async_write_some(asio::buffer(sessions[i]->sbuf), 
					bind(&Server::OnSend, this, _1));
			}
		}

		// 메세지를 보내온 클라이언트에게도 보낸다
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

		// 메세지 내용(방번호)이 정수가 아닐 경우
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
			SendAll(session, 0, "[" + session->id + "]" + "님이 종료하였습니다", false);
			SendAll(session, session->room_no, "[" + session->id + "]" + "님이 방을 나갔습니다", false);
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