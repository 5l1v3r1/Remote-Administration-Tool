//Client

#include "..\Library\tcp.h"

int main()
{
	const std::string ip = "10.7.240.254";
	const auto port = 1337;

	tcp_client main;

	main.set_port(port);

	main.set_ip(ip);

	if (main.startup())
	{
		std::cout << "Client has successfully started up\n";

		if (main.connect())
		{
			std::cout << "Client has connected to the server\n";

			std::string input;

			while (std::getline(std::cin, input))
			{
				std::cout << "Send Returned : " << main.send(input, "TEXT") << std::endl;
			}
		}
		else
		{
			std::cout << "Client could not connect to the server\n";
		}
	}
	else
	{
		std::cout << "Client could not start up\n";
	}

	std::cin.get();
}
