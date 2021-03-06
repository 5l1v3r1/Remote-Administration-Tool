// Server
#include "..\Library\precompile.h"
#include "..\Library\tcp_server.h"

int main()
{
	tcp_server main("472");

	pipe console = main.get_pipe();

	main.set_port(1337);

	main.set_prefix("Guest");

	if (main.startup())
	{
		console << "[+] Server startup sequence" << '\n';

		if (main.initialize())
		{
			console << "[+] Socket initialized" << '\n';

			if (main.bind())
			{
				console << "[+] Bound to port " << main.get_port() << "\n";

				if (main.listen())
				{
					console << "[+] Server is now listening" << '\n';

					if (main.manager())
					{
						console << "[+] Started client handler" << '\n';

						while (true)
						{
							std::cout << "server $ ";
							manip::command_line([&](std::string a, std::string b) {main.prompt(a, b); });
						}
					}

					console << "[-] Server could not start handler thread" << '\n';
				}
				else
				{
					console << "[-] Server could not start listening for connections" << '\n';
				}
			}
			else
			{
				console << "[-] Server could not bind to get_port {" << main.get_port() << "}" << '\n';
			}
		}
		else
		{
			console << "[-] Server could not initialize a socket" << '\n';
		}
	}
	else
	{
		console << "[-] Server could not be started" << '\n';
	}

	std::cin.get();
}
