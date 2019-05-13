#include <iostream>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <boost/thread/mutex.hpp>


using namespace boost;
using std::cout;
using std::endl;


class Client
{
	asio::ip::tcp::endpoint ep;
	asio::io_service ios;
	asio::ip::tcp::socket sock;
	boost::shared_ptr<asio::io_service::work> work;
	boost::thread_group thread_group;
	std::string sbuf;
	std::string rbuf;
	char buf[80];
	boost::mutex lock;

public:
	Client(std::string ip_address, unsigned short port_num) :
		ep(asio::ip::address::from_string(ip_address), port_num),
		sock(ios, ep.protocol()),
		work(new asio::io_service::work(ios))
	{}

	void Start()
	{
		for (int i = 0; i < 3; i++)
			thread_group.create_thread(bind(&Client::WorkerThread, this));

		// thread 잘 만들어질때까지 잠시 기다리는 부분
		this_thread::sleep_for(chrono::milliseconds(100));

		ios.post(bind(&Client::TryConnect, this));

		thread_group.join_all();
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

	void TryConnect()
	{
		cout << "[" << boost::this_thread::get_id() << "]" << " TryConnect" << endl;

		sock.async_connect(ep, boost::bind(&Client::OnConnect, this, _1));
	}

	void OnConnect(const system::error_code &ec)
	{
		cout << "[" << boost::this_thread::get_id() << "]" << " OnConnect" << endl;
		if (ec)
		{
			cout << "connect failed: " << ec.message() << endl;
			StopAll();
			return;
		}

		ios.post(bind(&Client::Send, this));
		ios.post(bind(&Client::Recieve, this));
	}

	void Send()
	{
		getline(std::cin, sbuf);

		sock.async_write_some(asio::buffer(sbuf), bind(&Client::SendHandle, this, _1));
	}

	void Recieve()
	{
		sock.async_read_some(asio::buffer(buf, 80), bind(&Client::ReceiveHandle, this, _1, _2));
	}

	void SendHandle(const system::error_code &ec)
	{
		if (ec)
		{
			cout << "async_read_some error: " << ec.message() << endl;
			StopAll();
			return;
		}

		Send();
	}

	void ReceiveHandle(const system::error_code &ec, size_t size)
	{
		if (ec)
		{
			cout << "async_write_some error: " << ec.message() << endl;
			StopAll();
			return;
		}

		if (size == 0)
		{
			cout << "Server wants to close this session" << endl;
			StopAll();
			return;
		}

		buf[size] = '\0';
		rbuf = buf;

		lock.lock();
		cout << rbuf << endl;
		lock.unlock();

		Recieve();
	}

	void StopAll()
	{
		sock.close();
		work.reset();
	}
};




int main()
{
	Client chat("127.0.0.1", 3333);
	chat.Start();

	return 0;
}