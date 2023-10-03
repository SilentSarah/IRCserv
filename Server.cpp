/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: yajallal <yajallal@student.1337.ma>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2023/09/24 16:17:16 by hmeftah           #+#    #+#             */
/*   Updated: 2023/10/03 14:44:02 by yajallal         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "Toolkit.hpp"
#include "Client.hpp"
 
/* === Coplien's form ===*/
Server::Server() : client_count(0)
{
	_bzero(&this->hints, sizeof(this->hints));
	this->server_socket_fd = -1;
	this->socket_data_size = sizeof(this->client_sock_data);
	this->clients.clear();
}


Server::Server(const Server& copy)
{
	(void) copy;
	_memset(&this->hints, (char *)&copy.hints, sizeof(copy.hints));
	this->server_socket_fd = copy.server_socket_fd;
	this->c_fd_queue = copy.c_fd_queue;
	this->client_fds = copy.client_fds;
	this->client_count = copy.client_count;
}

Server::Server(std::string port, std::string pass) {
	Server();
	CreateServer(port, pass);
}

Server &Server::operator=(const Server& copy)
{
	if (this != &copy) {
		_memset(&this->hints, (char *)&copy.hints, sizeof(copy.hints));
		this->server_socket_fd = copy.server_socket_fd;
		this->c_fd_queue = copy.c_fd_queue;
		this->client_fds = copy.client_fds;
		this->client_count = copy.client_count;
	}
	return (*this);
}

Server::~Server() {}

/* === Member Functions ===*/


/* 
 - Generates required data for the server to start.
 - ai_family: family of connections that will be used by the server (IPV4/IPV6 or Local Unix socket...)
 - ai_socktype: type of packets that will be sent and recieved by the server, SOCK_STREAM (most suitable for TCP)
				SOCK_DGRAM (suitable for UDP packets)
 - ai_flags: how will the server acquire it's address, AI_PASSIVE means it'll bind to any address there is, similar to 0.0.0.0
*/
bool	Server::GenerateServerData(const std::string &port) {
	this->hints.ai_family = AF_INET;
	this->hints.ai_socktype = SOCK_STREAM;
	this->hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, port.c_str(), &this->hints, &this->res)) {
		std::cerr << "Error: Couldn't acquire address info!" << std::endl;
		return 1;
	}
	return 0;
}

/*
 - Creates the socket and binds it to the desired address
 - socket() creates the socket based on the data generated by the function above.
 - setsockopt() sets socket options like consistent binding to the same port after quitting.
 - listen() sets the socket() generated fd to listen to a port (waiting for data to be written by the kernel).
 - fcntl() special function that controls how files work in linux, in this case we set it so the fds don't block the main thread after reading/writing
*/
bool	Server::CreateServer(const std::string &port, const std::string &pass) {
	int optval = 1;
	if (this->GenerateServerData(port))
		return 1;
	
	this->password = pass;
	this->server_socket_fd = socket(this->hints.ai_family, this->hints.ai_socktype, this->hints.ai_protocol);
	if (this->server_socket_fd == -1) {
		std::cerr << "Error: Socket creation has failed!" << std::endl;
		return 1;
	}
	setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR,  &optval, sizeof(optval));
	if (bind(this->server_socket_fd, this->res->ai_addr, this->res->ai_addrlen) == -1) {
		std::cerr << "Error: Couldn't bind the socket to host address!" << std::endl;
		return 1;
	}
	if (listen(this->server_socket_fd, MAX_IRC_CONNECTIONS) == -1) {
		std::cerr << "Cannot listen to port: " << port << std::endl;
		return 1;
	}
	std::cout << "Server has been successfully created for port " + port << std::endl;
	fcntl(server_socket_fd, F_SETFL, O_NONBLOCK);
	InsertSocketFileDescriptorToPollQueue(server_socket_fd);
	OnServerLoop();
	return 0;
}

/*
 - Inserts file descriptors generated by accept() into a queue for poll() function.
*/
void	Server::InsertSocketFileDescriptorToPollQueue(const int connection_fd) {

	struct pollfd tmp;

	tmp.fd = connection_fd;
	tmp.events = POLLIN | POLLOUT;
	this->c_fd_queue.push_back(tmp);
}

/*
 - Closes connection for all clients and cleans up remaining data and buffers
*/
void	 Server::CloseConnections(void) {
	for (size_t i = 0; i < this->client_fds.size(); i++) {
		if (this->client_fds.at(i) > 2)
			close(this->client_fds.at(i));
	}
	if (this->server_socket_fd > 2) {
		close(this->server_socket_fd);
	}
	PreformServerCleanup();
}

/*
 - Deletes client data after it disconnects or gets kicked.
*/
void	Server::PopOutClientFd(int client_fd) {
	std::vector<int>::iterator it = client_fds.begin();
	std::vector<Client>::iterator itc = clients.begin();
	std::vector<struct pollfd>::iterator itp = c_fd_queue.begin();
	
	while (it != client_fds.end()) {
		if (*it == client_fd) {
			client_fds.erase(it);
			break ;
		}
		it++;
	}
	while (itc != clients.end()) {
		if (itc->getSockID() == client_fd) {
			clients.erase(itc);
			break ;
		}
		itc++;
	}
	while (itp != c_fd_queue.end()) {
		if (itp->fd == client_fd) {
			c_fd_queue.erase(itp);
			break ;
		}
		itp++;
	}
}

void	Server::PreformServerCleanup(void) {
	freeaddrinfo(this->res);
}

/*
 - Wrapper function for PopOutClientFd
*/
void	Server::DeleteClient(int client_fd) {

	close(client_fd);
	PopOutClientFd(client_fd);
	this->client_count--;
}

/*
 - Copies socket data for each client, useful if you want to extract the ip later on
*/
void		Server::CopySockData(int client_fd) {
	this->clients.at(FindClient(client_fd)).client_sock_data = this->client_sock_data;
	this->clients.at(FindClient(client_fd)).socket_data_size = this->socket_data_size;
}

/*
 - Wrapper for many functions, it inserts the file descriptor generated by accept()
   into many objects for integrity since each client need to have it's data
   separated from the other.
*/
void	Server::InsertClient(int client_fd) {
		Client User(client_fd, 1);

		this->clients.push_back(User);
		CopySockData(client_fd);
		InsertSocketFileDescriptorToPollQueue(client_fd);
		send(client_fd, INTRO, _strlen(INTRO), 0);
		this->client_fds.push_back(client_fd);
		this->client_count++;
}

/*
	- Finds the right client index from the poll queue since poll queue has the socket fd
	  so the result might always be different by up to one index more than poll queue 
*/
int	Server::FindClient(int client_fd) {
	size_t i = 0;
	while (i < clients.size()) {
		if (clients.at(i).getSockID() == client_fd)
			return i;
		i++;
	}
	return -1;
}

/*
 	- Reads the input given by a certain client and stores it in a special buffer accessible only
      for that client.
*/
void	Server::ReadClientFd(int client_fd) {
	char	buf[MAX_IRC_MSGLEN];

	_bzero(buf, MAX_IRC_MSGLEN);
	int rb = recv(client_fd, buf, MAX_IRC_MSGLEN, MSG_PEEK);
	if (rb > 0) {
		while (SRH)
		{
			rb = recv(client_fd, buf, MAX_IRC_MSGLEN, 0);
			if (rb <= 0) {
				break ;
			} else {
				buf[rb] = 0;
				raw_data += buf;
			}
		}
		clients.at(FindClient(client_fd)).SetBuffer(raw_data);
	}
}

/*
	- Sends the message to a client, in this case it'll be used to send messages
	  to all clients connected.
*/
void	Server::SendClientMessage(int client_fd) {
	if (!clients.at(FindClient(client_fd)).GetMessageBuffer().empty()) {
		send(client_fd, clients.at(FindClient(client_fd)).GetMessageBuffer().c_str(), clients.at(FindClient(client_fd)).GetMessageBuffer().length() - 1, 0);
	}
	send_buffer.clear();
}

/*
	- Checks whether the client has just connected.
*/
bool	Server::JustConnected(int socketfd) {
	size_t i = 0;

	if (!this->clients.empty()) {
		while (i < this->clients.size()) {
			if (socketfd == clients[i].getSockID()) {
				return clients[i].JustConnectedStatus();
			}
			i++;
		}
	}
	return 0;
}

/*
 - Kicks clients that should be kicked, there's a function to set client's kick status.
*/
void	Server::KickClients(void) {
	std::vector<Client>::iterator it = clients.begin();

	while (it != clients.end()) {
		if (it->ShouldBeKicked() == true) {
			DeleteClient(it->getSockID());
		}
		it++;
	}
}

/*
 - Authenticates the client.
 - If the client fails authentication check, that same client will be kicked.
 - it works by takes the PASS command and checks the argument following it
   whether it matches server's password or not.
*/
void	Server::Authenticate(int client_fd) {
	char	*pass;
	std::string hold_pass;
	size_t pos;
	size_t index;

	index = FindClient(client_fd);
	if (index >= 0) {
		pass = std::strtok(const_cast<char *>(clients.at(index).GetBuffer().c_str()), "\r\n");
		while (pass != NULL) {
			hold_pass = pass;
			if ((pos = hold_pass.find("PASS", 0)) != std::string::npos) {
				hold_pass = hold_pass.substr(pos + 5, hold_pass.length());
				break ;
			}
			pass = std::strtok(NULL, "\r\n");
		}
		if (hold_pass != password) {
			DeleteClient(client_fd);
			return ;
		}
		clients.at(index).SetJustConnectedStatus(false);
	}
}

/*
	- Iterates over all file descriptors registered into poll() queue
	- Checks if the client has written anything in it's fd, if it did
	  then it'll read it and call ReadClientFd().
	- Checks if client's fd is available to written to the server
	  will preform checks and sends the appropriate message back
	  to the client.
	- Cleans all buffers after each iteration to insure consistency.
	- Finally it checks if the client disconnects, if it happens, it will
	  delete all client data including fd from the server.
*/
void	Server::OnServerFdQueue(void) {
	for (size_t i = 1; i < this->client_count + 1; i++) {
		if (this->c_fd_queue[i].revents == (POLLIN | POLLHUP)) {
			std::cout << "Client has disconnected, IP: " << inet_ntoa(this->client_sock_data.sin_addr) << std::endl;
			DeleteClient(c_fd_queue[i].fd);
		}
		else if (this->c_fd_queue[i].revents & POLLIN) {
			ReadClientFd(this->c_fd_queue[i].fd);
			if (JustConnected(c_fd_queue[i].fd)) {
				Authenticate(c_fd_queue[i].fd);
				continue ;
			}
			Interpreter(c_fd_queue[i].fd);
		}
		else if (this->c_fd_queue[i].revents & POLLOUT) {
			SendClientMessage(c_fd_queue[i].fd);
		}
		raw_data.clear();
		send_buffer.clear();
	}
}

/*
	- Non-ending loop that accepts and registers client data.
	- poll() a function that allows for events that happened
	  in all fds, which means you can check multiple fds for
	  write/read events, (if a fd has data to read from or if it's available to write on)
	- Registers file descriptors into a poll queue for it later to be checked by OnServerFdQueue()
*/
void	Server::OnServerLoop(void) {
	while (SRH) {
	int 	new_client_fd = -1;
	int 	poll_num = poll(&this->c_fd_queue[0], this->client_count + 1, 0);
	
	new_client_fd = accept(this->server_socket_fd, (struct sockaddr *)&this->client_sock_data, &this->socket_data_size);
	if (new_client_fd > 0) {
		fcntl(new_client_fd, F_SETFL, O_NONBLOCK);
		std::cout << "Connected IP: " << inet_ntoa(this->client_sock_data.sin_addr) << std::endl;
		InsertClient(new_client_fd);
		std::cout << "Total Clients: " << clients.size() << std::endl;
	}
	if (poll_num > 0)
		OnServerFdQueue();
	}
}


/* ========== INTERPRETER SECTION =========== */

// void	Server::PONG(int client_fd) {
// 	std::string client_ip = inet_ntoa(clients.at(FindClient(client_fd)).client_sock_data.sin_addr);
// 	send_buffer += "PONG " + client_ip + " " + command.begin()->second + "\r\n";
// 	clients.at(client_fd).SetMessage(send_buffer);
// }

// void	Server::FindCommand(int client_fd) {
// 	int	command_choice = 0;
// 	std::string Commands[1] = { "PING" };
// 	void (Server::*Command[1])(int) = { &Server::PONG };

// 	std::map<std::string, std::string>::iterator it = this->command.begin();
// 	while (it != this->command.end()) {
// 		std::cout << it->first << std::endl;
// 		if (it->first == Commands[command_choice]) {
// 			(this->*Command[command_choice])(client_fd);
// 			return ;
// 		}
// 		command_choice++;
// 	}
// }

/*
	- Note to self: Tokenizer seems somewhat done. All i need to do now is
	to parse more 

*/
void	Server::Interpreter(int __unused client_fd) {
	char *token = std::strtok(const_cast<char *>(raw_data.c_str()), "\r\n");
	while (token) {
		std::string command_temp;
		std::string tmp;
		std::vector<std::string> args;
		std::stringstream holder(token);
	
		std::cout << raw_data << std::endl;
		std::getline(holder, command_temp, ' ');
		while (getline(holder, tmp, ' '))
			args.push_back(tmp);
		command.insert(std::pair<std::string, std::vector<std::string> >(command_temp, args));
		// std::map<std::string, std::vector<std::string> >::iterator it = this->command.begin();
		//FindCommand(client_fd);
		token = std::strtok(NULL, "\r\n");
		std::cout << command.begin()->first << std::endl;
		for (size_t i = 0; i < command.begin()->second.size(); i++)
			std::cout << command.begin()->second[i] << std::endl;	
		command.clear();
	}
}

void Server::JOIN(int client_fd)
{
	int client_id =  FindClient(client_fd);
	std::map< std::string, std::vector<int> >::iterator channel_found = this->channels.find(this->command.begin()->second[0]);
	if (channel_found == this->channels.end())
		this->clients.at(client_id).SetMessage(this->clients.at(client_id).getName() + " " + this->command.begin()->first + " : No such channel\r\n");
	else if (std::find(channel_found->second.begin(), channel_found->second.end(), client_fd) != channel_found->second.end())
		this->clients.at(client_id).SetMessage(this->clients.at(client_id).getName() + " :You are already in this channel\r\n");
	else
	{
		channel_found->second.push_back(client_fd);
		this->clients.at(client_id).SetMessage(this->clients.at(client_id).getName() + " :is joining the channel\r\n");
	}
}