#include "helpers.cpp"
#include <arpa/inet.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

#define max_buf_size 5120
#define server_port 4950
#define max_n_threads 20

char receiver_buffer[max_buf_size], buf[max_buf_size];
char curr_no_of_clients = 0;

void service_socket(int client_fd, fd_set *all_fds, int server_soc_fd, int last_fd)
{
	int rec_buffer_size;
	int clock[max_n_threads];
	int s_thread, r_thread;
	int i = 0;

	my_supernode recv_vote;

	if ((rec_buffer_size = recv(client_fd, receiver_buffer, max_buf_size, 0)) == 0)
	{ //client wants to close connection
		close(client_fd);
		FD_CLR(client_fd, all_fds); //set 0 to bit corresponding to this clients fd
		curr_no_of_clients--;
	}
	else
	{
		// a client has pushed a message to server, broadcast to all other active sockets

		parse_single_vote(recv_vote, receiver_buffer);
		cout << "got vote from thread: " << recv_vote.curr_negotiator << " with destination: " << recv_vote.curr_client << endl;

		// if (recv_message.s_thread == -1)
			// return;

	

		for (int j = 0; j <= last_fd; j++)
		{																	  //broadcast this to all other connected clients
			if (FD_ISSET(j, all_fds) && j != server_soc_fd && j != client_fd){
				
				send(j, receiver_buffer, rec_buffer_size, 0);				  //send message to client with active fd
				i++;
			}
				
		}
		memset(receiver_buffer, 0, max_buf_size);
		cout << "broadcasted to: " << i << endl;
	}
}

void accept_client_connection(fd_set *all_fds, int *last_fd, int server_soc_fd, struct sockaddr_in client_addr)
{
	int client_fd;
	socklen_t client_addr_len;
	client_addr_len = sizeof(struct sockaddr_in);

	if (curr_no_of_clients == max_n_threads)
	{
		cout << "Reached Max no of clients, cannot accept more!" << endl;
		return;
	}

	if ((client_fd = accept(server_soc_fd, (struct sockaddr *)&client_addr, &client_addr_len)) < 0)
	{
		cout << "Failed to accept client" << endl;
		exit(1);
	}
	else
	{
		FD_SET(client_fd, all_fds); //set the client fd to 1
		curr_no_of_clients++;

		if (client_fd > *last_fd) // update the last fd/ no of active dfs
			*last_fd = client_fd;

		cout << "connected to new client!" << endl;
	}
}

int main()
{

	int flag = 1, server_soc_fd = 0, last_fd, i; // last fd stores the
	fd_set all_fds, current_fds;

	struct sockaddr_in server_addr, client_addr;

	FD_ZERO(&all_fds);	   //a bit array of all the socket fds
	FD_ZERO(&current_fds); //a bit array of the servicable fds

	if ((server_soc_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		cout << "Failed to init socket" << endl;
		exit(1);
	}

	// INIT SERVER ADDR
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(4950);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	memset(server_addr.sin_zero, '\0', sizeof server_addr.sin_zero);

	//INIT SOCKET
	if (setsockopt(server_soc_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) < 0)
	{
		cout << " Failed to set socket options" << endl;
		exit(1);
	}

	//BIND SOCKET (removed check for c++11)
	bind(server_soc_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr));

	//START LISTENING
	if (listen(server_soc_fd, 10) < 0)
	{
		cout << "Failed to start listening" << endl;
		exit(1);
	}

	cout << "Started listening on port 4950...";
	cout.flush(); //empty stdout buffer

	FD_SET(server_soc_fd, &all_fds); //set server fd to 1 since server is now active
	last_fd = server_soc_fd;		 //currently only server fd is present, so last_fd = 1

	while (true)
	{

		current_fds = all_fds; //update current set of fds, incase an fd has been added/ closed

		if (select(last_fd + 1, &current_fds, NULL, NULL, NULL) < 0)
		{ //once an fd is set, condition will break
			cout << "error occured during select()" << endl;
			exit(1);
		}

		for (i = 0; i < last_fd + 1; i++)
		{ //iterate through list of fds
			if (FD_ISSET(i, &current_fds))
			{							//check which socket sent data
				if (i == server_soc_fd) // server accepting new client
					accept_client_connection(&all_fds, &last_fd, server_soc_fd, client_addr);
				else
					service_socket(i, &all_fds, server_soc_fd, last_fd); // a client has sent data to server
			}
		}
	}
	cout << "done";
	return 0;
}