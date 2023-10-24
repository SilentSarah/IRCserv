/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hmeftah <hmeftah@student.1337.ma>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2023/09/24 16:17:16 by hmeftah           #+#    #+#             */
/*   Updated: 2023/10/24 15:16:01 by hmeftah          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "Toolkit.hpp"
#include "Client.hpp"
#include "Parse.hpp"
/* === Coplien's form ===*/
Server::Server() : client_count(0)
{
	_bzero(&this->hints, sizeof(this->hints));
	this->server_socket_fd = -1;
	this->socket_data_size = sizeof(this->client_sock_data);
	this->clients.clear();
	this->_setChannels();
	// this->clients.reserve(MAX_IRC_CONNECTIONS);
}


Server::Server(const Server& copy)
{
	(void) copy;
	_memset(&this->hints, (char *)&copy.hints, sizeof(copy.hints));
	this->server_socket_fd = copy.server_socket_fd;
	this->c_fd_queue = copy.c_fd_queue;
	this->client_fds = copy.client_fds;
	this->client_count = copy.client_count;
	this->_setChannels();
	// this->clients.reserve(MAX_IRC_CONNECTIONS);
}

Server::Server(std::string port, std::string pass) {
	Server();
	CreateServer(port, pass);
	this->_setChannels();
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
    this->hints.ai_protocol = IPPROTO_TCP;
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
	this->server_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
	std::list<Client>::iterator itc = clients.begin();
	std::vector<struct pollfd>::iterator itp = c_fd_queue.begin();
	
	while (it != client_fds.end()) {
		if (*it == client_fd) {
			client_fds.erase(it);
			break ;
		}
		++it;
	}
	while (itc != clients.end()) {
		if (itc->getSockID() == client_fd) {
			clients.erase(itc);
			break ;
		}
		++itc;
	}
	while (itp != c_fd_queue.end()) {
		if (itp->fd == client_fd) {
			c_fd_queue.erase(itp);
			break ;
		}
		++itp;
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
void	Server::CopySockData(int client_fd) {
    std::list<Client>::iterator itc = clients.begin();

    while (itc != clients.end()) {
        if (itc->getSockID() == client_fd) {
            itc->client_sock_data = this->client_sock_data;
            itc->socket_data_size = this->socket_data_size;
        }
        ++itc;
    }
}

/*
 - Wrapper for many functions, it inserts the file descriptor generated by accept()
   into many objects for integrity since each client need to have it's data
   separated from the other.
*/
void	Server::InsertClient(int client_fd) {
		Client User(client_fd, 1);

        fcntl(client_fd, F_SETFL, O_NONBLOCK);
		this->clients.push_back(User);
		CopySockData(client_fd);
		InsertSocketFileDescriptorToPollQueue(client_fd);
		//send(client_fd, INTRO, _strlen(INTRO), 0);
		this->client_fds.push_back(client_fd);
		this->client_count++;
}

/*
	- Finds the right client index from the poll queue since poll queue has the socket fd
	  so the result might always be different by up to one index more than poll queue 
*/
int		Server::FindClient(int client_fd) {
    size_t                      i = 0;
	std::list<Client>::iterator it = clients.begin();
	while (it != clients.end()) {
		if (it->getSockID() == client_fd)
			return i;
		i++;
        ++it;
	}
	return -1;
}

/*
 	- Reads the input given by a certain client and stores it in a special buffer accessible only
      for that client.
*/
void 	Server::ReadClientFd(int client_fd) {
    char buf[MAX_IRC_MSGLEN];
    _bzero(buf, MAX_IRC_MSGLEN);
    while (SRH) {
        int rb = recv(client_fd, buf, MAX_IRC_MSGLEN, 0);
        if (rb > 0) {
            buf[rb] = 0;
            raw_data += buf;
        } else if (rb <= 0) {
            std::list<Client>::iterator it = clients.begin();
            std::advance(it, FindClient(client_fd));
            it->SetBuffer(raw_data);
            std::cout << "Buffer Read Data from: " << it->getSockID() << "\n" + it->GetBuffer() << std::endl;
            break ;
        }
    }
}

/*
	- Sends the message to a client, in this case it'll be used to send messages
	  to all clients connected.
*/
void	Server::SendClientMessage(int client_fd) {
    std::list<Client>::iterator it = clients.begin();
    std::advance(it, FindClient(client_fd));

	if (!it->GetMessageBuffer().empty()) {
		send(client_fd, it->GetMessageBuffer().c_str(), it->GetMessageBuffer().length(), 0);
	}
    it->SetMessage("");
	send_buffer.clear();
}

/*
	- Checks whether the client has just connected.
*/
bool	Server::JustConnected(int socketfd) {
	std::list<Client>::iterator it = clients.begin();

	if (!this->clients.empty()) {
		while (it != clients.end()) {
			if (socketfd == it->getSockID()) {
				return it->JustConnectedStatus();
			}
			++it;
		}
	}
	return 0;
}

/*
 - Kicks clients that should be kicked, there's a function to set client's kick status.
*/
void	Server::KickClients(void) {
	std::list<Client>::iterator it = clients.begin();

	while (it != clients.end()) {
		if (it->ShouldBeKicked() == true) {
			DeleteClient(it->getSockID());
		}
		++it;
	}
}
/*
    -   Checks whether the data is valid or not (ending with "\\r\\n")
*/
bool    Server::CheckDataValidity(void) {
    return (raw_data.find("\r\n") != std::string::npos);
}

// bool   Server::CheckLoginTimeout(int client_fd) {
//     if (time(NULL) - clients.at(FindClient(client_fd)).GetConnectedDate() > MAX_TIMEOUT_DURATION)
//         return true;
//     return false;
// }

bool    Server::CheckConnectDataValidity(int client_fd) {
    
	std::string str = std::find(clients.begin(), clients.end(), client_fd)->GetBuffer();
    char *temp = std::strtok(const_cast<char *>(str.c_str()), "\r\n");
    while (temp) {
        std::string tmp_str = temp, tmp_compare;
        std::stringstream tmp_stream;
        tmp_stream << tmp_str;
        std::getline(tmp_stream, tmp_compare, ' ');
        if (tmp_compare == "PASS")
            return true;
        temp = std::strtok(NULL, "\r\n");
    }
    return false;
}

/*
 - Authenticates the client.
 - If the client fails authentication check, that same client will be kicked.
 - it works by takes the PASS command and checks the argument following it
   whether it matches server's password or not.
*/
void	Server::Authenticate(int client_fd) {
	char	*pass;
	std::string hold_pass, temp_pass;
    std::string hold_user;
    std::stringstream hold_nick_temp;
    std::string tmp[4];
    std::list<Client>::iterator it = std::find(clients.begin(), clients.end(), client_fd);
	size_t pos;
	int index;

	index = FindClient(client_fd);
    // if (CheckLoginTimeout(client_fd)) {
    //     if (temp_pass != password) {
	// 	    std::cout << "Client Couldn't Authenticate in time." << std::endl;
    //         std::cout << "Timeout at: " << time(NULL) - clients.at(index).GetConnectedDate() << std::endl;
	// 		DeleteClient(client_fd);
	// 		return ;
	// 	}
    // }
    if (CheckConnectDataValidity(client_fd)) {
	    if (index >= 0) {
	    	pass = std::strtok(const_cast<char *>(std::find(clients.begin(), clients.end(), client_fd)->GetBuffer().c_str()), "\r\n");
	    	while (pass != NULL) {
	    		hold_pass = pass;
	    		if ((pos = hold_pass.find("PASS", 0)) != std::string::npos) {
	    			temp_pass = hold_pass.substr(pos + 5, hold_pass.length());
	    		}
                else if (((pos = hold_pass.find("NICK", 0)) != std::string::npos)) {
                    hold_nick_temp << hold_pass.substr(pos + 5, hold_pass.length());
                    std::getline(hold_nick_temp, hold_user, ' ');
                }
                else if (((pos = hold_pass.find("USER", 0)) != std::string::npos)) {
                    hold_nick_temp.clear();
                    hold_nick_temp << hold_pass.substr(pos + 5, hold_pass.length());
                    for (size_t i = 0; i < 4; i++) {
                        std::getline(hold_nick_temp, tmp[i], ' ');
                    }
                    it->SetName(tmp[0]);
                    it->SetHostname(tmp[1]);
                    it->SetServername(tmp[2]);
                    tmp[3].erase(tmp[3].find(":", 0), 1);
                    it->SetRealname(tmp[3]);
            		it->SetJustConnectedStatus(false);
					raw_data.clear();
    
                    // std::cout << "Name: " << clients.at(FindClient(client_fd)).getName() << std::endl;
                    // std::cout << "HostName: " << clients.at(FindClient(client_fd)).getHostname() << std::endl;
                    // std::cout << "ServerName: " << clients.at(FindClient(client_fd)).getServername() << std::endl;
                    // std::cout << "RealName: " << clients.at(FindClient(client_fd)).getRealname() << std::endl;
                    
                }
	    		pass = std::strtok(NULL, "\r\n");
	    	}
            it->SetNick(hold_user);
	    	if (temp_pass != password) {
	    		std::cout << "PASSWORD DOES NOT MATCH " + temp_pass << std::endl;
	    		DeleteClient(client_fd);
	    		return ;
	    	}
	    }
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
	// std::list<Channel>::iterator channel_it;
	for (size_t i = 0; i < this->c_fd_queue.size(); i++) {
		if (this->c_fd_queue[i].revents == (POLLIN | POLLHUP)) {
			std::cout << "Client has disconnected, IP: " << inet_ntoa(this->client_sock_data.sin_addr) << std::endl;
			// channel_it = this->_channels.begin();
			// for (; channel_it != this->_channels.end(); ++channel_it)
			// 	channel_it->removeMember()
			/* 
				remove the disconnected ip from all channels 
			*/
			DeleteClient(c_fd_queue[i].fd);
		}
		else if (this->c_fd_queue[i].revents & POLLIN) {
            if (this->c_fd_queue[i].fd == this->server_socket_fd) {
                int new_client_fd = -1;
                do {
                    new_client_fd = accept(this->server_socket_fd, (struct sockaddr *)&this->client_sock_data, &this->socket_data_size);
	                if (new_client_fd > 0) {
	                	std::cout << "Connected IP: " << inet_ntoa(this->client_sock_data.sin_addr) << std::endl;
	                	InsertClient(new_client_fd);
	                	std::cout << "Total Clients: " << clients.size() << std::endl;
	                }
                }
                while (new_client_fd > 0);
                continue;
            }
			ReadClientFd(this->c_fd_queue[i].fd);
            if (CheckDataValidity()) {
			    if (JustConnected(c_fd_queue[i].fd))
				{
			    	Authenticate(c_fd_queue[i].fd);
					// std::cout << clients.at(FindClient(c_fd_queue[i].fd));
				}
				else {
                    try {
			    	    Interpreter(c_fd_queue[i].fd);
                        raw_data.clear();
                    } catch (Server::ClientQuitException &e) {
                        DeleteClient(c_fd_queue[i].fd);
                        std::cout << "Client has disconnected, IP: " << inet_ntoa(this->client_sock_data.sin_addr) << std::endl;
                        break ;
                    }
                }
            }
		}
		else if (this->c_fd_queue[i].revents & POLLOUT) {
			SendClientMessage(c_fd_queue[i].fd);
            send_buffer.clear();
		}
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
	int 	poll_num = poll(&this->c_fd_queue[0], this->c_fd_queue.size(), 0);

	if (poll_num > 0)
		OnServerFdQueue();
	}
}
/**
 * Prints the command data from the given Parse object.
 *
 * @param Data the Parse object containing the command data
 *
 * @return void
 *
 * @throws None
 */
void    Server::PrintCommandData(Parse &Data) {
    std::vector<std::string> tmp = Data.getTarget();
    std::vector<std::string>::iterator it = tmp.begin();
	std::cout << "raw buffer : " << Data.getClient().GetBuffer() << std::endl;
    std::cout << "- Command: " + Data.getCommand() << std::endl;
    std::cout << "- Targets: " << std::endl;
    while (it != tmp.end())
        std::cout << "      - " + *(it++) << std::endl;
    std::cout << "- Args: " << std::endl;
    tmp = Data.getArgs();
    it = tmp.begin();
    while (it != tmp.end())
        std::cout << "      - " + *(it++) << std::endl;
    std::cout << "- Message: " + Data.getMessage() << std::endl;
    std::cout << "- Type: " << (Data.getType() ? "MSGINCLUDED" : "MSGNOTINCLUDED") << std::endl;
    std::cout << std::endl;
}

void  	Server::CreateCommandData(int client_fd, CommandType type) {
    std::list<Client>::iterator xit = std::find(clients.begin(), clients.end(), client_fd);
    std::string str = xit->GetBuffer();
    std::string Accumulated_Message(str);
    std::vector<std::string> args;
    std::vector<std::string> targets;
    
	str.replace(str.find("\r\n"), 2, "");
    targets.clear();
    char    *token = std::strtok(const_cast<char *>(str.c_str()), " ");
    if (token)
        this->_data->setCommand(token);
    while (token) {
        token = std::strtok(NULL, " ");
        if (token)
            args.push_back(token);
    }
    std::vector<std::string>::iterator it = args.begin();
    if (type == MSGINCLUDED) {
        while (it != args.end()) {
            size_t pos = it->find(":");
            if (pos != std::string::npos) {
                it->erase(pos, 1);
                pos = Accumulated_Message.find(":", 0);
                if (pos != std::string::npos) {
                    Accumulated_Message = Accumulated_Message.substr(pos + 1, Accumulated_Message.length() - (pos + 1));
                }
                this->_data->setMessage(Accumulated_Message);
                break ;
            } else
                targets.push_back(*it);
            it++;
        }
        this->_data->setTarget(targets);
        args.clear();
    } else if (type == MSGNOTINCLUDED) {
        this->_data->setArgs(args);
        this->_data->setMessage("");
        this->_data->setTarget(targets);
        this->_data->setType(MSGNOTINCLUDED);
    }
    this->_data->setType(type);
}

/*
	- Note to self: Tokenizer seems somewhat done. All i need to do now is
	to parse more 

*/
void	Server::Interpreter(int client_fd) 
{
    std::list<Client>::iterator xit = std::find(clients.begin(), clients.end(), client_fd);
    this->_data = new Parse(*xit);
	if (xit->GetBuffer().find(":", 0) != std::string::npos) {
		CreateCommandData(client_fd, MSGINCLUDED);
	} else {
		CreateCommandData(client_fd, MSGNOTINCLUDED);
	}
	PrintCommandData(*(this->_data));
	if (this->_data->getCommand() == "NICK")
		this->nick();
	else if (this->_data->getCommand() == "JOIN")
		this->join();
	else if (this->_data->getCommand() == "WHO")
		this->who();
	else if (this->_data->getCommand() == "MODE")
		this->mode();
	else if (this->_data->getCommand() == "PRIVMSG")
		this->privMsg();
	else if (this->_data->getCommand() == "TOPIC")
		this->topic();
	else if (this->_data->getCommand() == "INVITE")
		this->invite();
	else if (this->_data->getCommand() == "KICK")
		this->kick();
    else if (this->_data->getCommand() == "QUIT") {
        throw(Server::ClientQuitException());
    }
	raw_data.clear();
	xit->SetBuffer("");
}

void	Server::nick()
{
	Client&		client = this->_data->getClient();
	std::string	nickname;
	if (this->_data->getArgs().size() != 0)
	{
		std::string  nickname = this->_data->getArgs().at(0);
		client.SetMessage(_user_info(client, true) + "NICK" + " :" + nickname + "\r\n");
		client.SetNick(nickname);
	}
}

void	Server::join()
{
	std::string 					channel_name;
	std::string 					channel_password;						
	std::list<Channel>::iterator	channel_it;
	Client&							client = this->_data->getClient();

	channel_name = this->_data->getArgs().at(0);
	channel_password = ( this->_data->getArgs().size() == 2 ? this->_data->getArgs().at(1) : "");
	channel_it = std::find((*this)._channels.begin(), (*this)._channels.end(), channel_name);
	if (channel_it != (*this)._channels.end())
	{
		if (channel_it->getHasPassword())
		{
			if (channel_it->getPassword() == channel_password)
				channel_it->join(client);
			else
				client.SetMessage(_user_info(client, false) + ERR_BADCHANNELKEY(client.getName(), channel_it->getName()));
		}
		else
			channel_it->join(client);
	}
	else
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHCHANNEL(client.getNick(), channel_name));
	std::cout << "Command -> " << this->_data->getCommand() << "\nmessage to send : " << client.GetMessageBuffer() << std::endl;
}

void	Server::_setChannels()
{
	this->_channels.push_back(Channel("#yajallal", "yajallal"));
	this->_channels.push_back(Channel("#mkhairou", "mkhairou"));
	this->_channels.push_back(Channel("#hmeftah", "hmeftah"));
	this->_channels.push_back(Channel("#random"));
	this->_channels.push_back(Channel("#general"));
}

void	Server::who()
{
	
	std::list<Channel>::iterator	channel_it;
	std::string						message_to_send;
	Client&							client = this->_data->getClient();
	if ( this->_data->getArgs().size() > 0)
	{
		const std::string&			first_arg_type = this->_data->getArgs().at(0);
		if (first_arg_type.at(0) == '#')
		{
			channel_it = std::find(this->_channels.begin(), this->_channels.end(), first_arg_type);
			if (channel_it != this->_channels.end())
				channel_it->who(client);
			else
				client.SetMessage(_user_info(client, false) + RPL_ENDOFWHO(client.getNick(), first_arg_type));
		}
	}
	std::cout << "Command -> " << this->_data->getCommand() << "\nmessage to send : " << client.GetMessageBuffer() << std::endl;
}

void	Server::set_remove_mode(Client& client ,std::list<Channel>::iterator channel_it)
{
	std::string						modes = this->_data->getArgs().at(1);
	bool							add_remove = true;
	std::list<Client>::iterator	member_it;
	std::string						mode_param = (this->_data->getArgs().size() == 3 ? this->_data->getArgs().at(2) : "");

	for (size_t i = 0; i < modes.size(); i++)
	{
		if (modes.at(i) == '-')
			add_remove = false;
		else if (modes.at(i) == '+')
			add_remove = true;
		else if (std::strchr("itlk", modes.at(i)))
		{
			if (this->_data->getArgs().size() == 3)
				channel_it->channelMode(client, add_remove, modes.at(i), mode_param);
			else
				channel_it->channelMode(client, add_remove, modes.at(i), mode_param);
		}
		else if (modes.at(i) == 'o')
		{
			if (!mode_param.empty())
			{
				member_it = std::find(this->clients.begin(), this->clients.end(), mode_param);
				if (member_it == this->clients.end())
					client.SetMessage(_user_info(client, false) + ERR_NOSUCHNICK(client.getNick(), mode_param));
				else
					channel_it->memberMode(client, add_remove, 'o', *member_it);
			}
		}
		else
			ERR_UNKNOWNMODE(_user_info(client, false) + client.getNick(), modes.at(i));
	}
}
void	Server::mode()
{
	this->PrintCommandData(*(this->_data));   
	Client&							client = this->_data->getClient();
	const std::string&				target_name = this->_data->getArgs().at(0);
	std::list<Channel>::iterator	channel_it;
	if (target_name.at(0) == '#')
	{
		channel_it = std::find(this->_channels.begin(), this->_channels.end(), target_name);
		if (channel_it == this->_channels.end())
			client.SetMessage(_user_info(client, false) + ERR_NOSUCHCHANNEL(client.getNick(), target_name));
		else
		{
			if (this->_data->getArgs().size() == 1)
				channel_it->mode(client);
			else
				this->set_remove_mode(client, channel_it);	
		}
	}
	std::cout << "Command -> " << this->_data->getCommand() << "\nmessage to send : " << client.GetMessageBuffer() << std::endl;
}

void	Server::privMsg()
{
	Client& 						client = this->_data->getClient();
	size_t 							pos;
	bool							send_to_operator;
	bool							send_to_founder;
	std::list<Channel>::iterator	channel_it;
	std::list<Client>::iterator	client_it;
	std::string						msg_to_send;
	std::string						target;
	if (this->_data->getTarget().size() == 0)
		client.SetMessage(_user_info(client, false) + ERR_NORECIPIENT(client.getNick(), "PRIVMSG"));
	else if (this->_data->getMessage().empty())
			client.SetMessage(_user_info(client, false) + ERR_NOTEXTTOSEND(client.getNick()));
	else
	{
		target = this->_data->getTarget().at(0);
		pos = target.find('#');
		send_to_operator = false;
		send_to_founder = false;
		if (pos != std::string::npos)
		{
			for (size_t i = 0; i < pos; i++)
			{
				if (target.at(i) == '@')
				{
					send_to_operator = true;
					target.erase(i, 1);
				}
				else if (target.at(i) == '~')
				{
					send_to_founder = true;
					target.erase(i, 1);
				}
			}
			channel_it = std::find(this->_channels.begin(), this->_channels.end(), target);	
			if (channel_it == this->_channels.end())
				client.SetMessage(_user_info(client, false) + ERR_NOSUCHNICK(client.getNick(), target));
			else
			{
				msg_to_send = ":" + client.getNick() + "!" + client.getName() + "@" + client.getHostname() + " PRIVMSG " + target + " :"+ this->_data->getMessage() + "\r\n";
				if (send_to_operator)
					channel_it->sendToOperators(client, msg_to_send);
				else if(send_to_founder)
					channel_it->sendToFounder(client, msg_to_send);
				else 
					channel_it->sendToAll(client, msg_to_send);
			}
		}
		else
		{
			client_it = std::find(this->clients.begin(), this->clients.end(), target);
			msg_to_send = ":" + client.getNick() + "!" + client.getName() + "@" + client.getHostname() + " PRIVMSG " + target + " :"+ this->_data->getMessage() + "\r\n";
			if (client_it == this->clients.end())
				client.SetMessage(_user_info(client, false) + ERR_NOSUCHNICK(client.getNick(), target));
			else
				client_it->SetMessage(msg_to_send);
		}
	}
	std::cout << "Command -> " << this->_data->getCommand() << "\nmessage to send : " << client.GetMessageBuffer() << std::endl;
}


void 	Server::topic()
{
	this->PrintCommandData(*this->_data);
	Client&							client = this->_data->getClient();
	std::string						target;
	std::list<Channel>::iterator	channel_it;

	target = this->_data->getMessage().empty() ? this->_data->getArgs().at(0) : this->_data->getTarget().at(0);
	channel_it = std::find(this->_channels.begin(), this->_channels.end(), target);
	if (channel_it == this->_channels.end())
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHCHANNEL(client.getNick(), target));
	else
		channel_it->topic(client, !this->_data->getMessage().empty(), this->_data->getMessage());
}

void	Server::invite()
{
	std::list<Client>::iterator	target_it;
	std::list<Channel>::iterator	channel_it;
	Client&							client = this->_data->getClient();
	const std::string&				target = this->_data->getArgs().at(0);
	const std::string&				channel = this->_data->getArgs().at(1);

	target_it = std::find(this->clients.begin(), this->clients.end(), target);
	channel_it = std::find(this->_channels.begin(), this->_channels.end(), channel);
	if (target_it == this->clients.end())
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHNICK(client.getNick(), target));
	else if (channel_it == this->_channels.end())
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHCHANNEL(client.getNick(), channel));
	else
		channel_it->invite(client, *target_it);
}
void	Server::kick()
{
	std::string						target_name;
	std::string 					channel_name;
	std::list<Client>::iterator	target_it;
	std::list<Channel>::iterator	channel_it;
	Client&							client = this->_data->getClient();
	
	channel_name = (this->_data->getMessage().empty() ? this->_data->getArgs().at(0) : this->_data->getTarget().at(0));
	target_name = (this->_data->getMessage().empty() ? this->_data->getArgs().at(1) : this->_data->getTarget().at(1));
	
	channel_it = std::find(this->_channels.begin(), this->_channels.end(), channel_name);
	target_it = std::find(this->clients.begin(), this->clients.end(), target_name);
	if (channel_it == this->_channels.end())
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHCHANNEL(client.getNick(), channel_name));
	else if (target_it == this->clients.end())
		client.SetMessage(_user_info(client, false) + ERR_NOSUCHNICK(client.getNick(), target_name));
	else
		channel_it->kick(client, *target_it, this->_data->getMessage());
}

/*-------------------- handle space in message ------------------------*/
/*-------------------- check first that there is a falid mode ------------------------*/
/*-------------------- remover memeber_prifixes function ------------------------*/
