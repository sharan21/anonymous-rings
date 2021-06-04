#include "helpers.cpp"

using namespace std;

#define max_buf_size 5120
#define server_port 4950
#define default_delay 1000

void service_socket(char sender_buffer[], int server_sock_fd, my_supernode &s_node, my_supernode &recv_snode)
{
	char receiver_buffer[max_buf_size];
	int message_size = recv(server_sock_fd, receiver_buffer, max_buf_size, 0);
	receiver_buffer[message_size] = '\0';

	// parse_received_messages(receiver_buffer, server_sock_fd, s_node);
	parse_received_votes(receiver_buffer, server_sock_fd, s_node, recv_snode);
}

int main()
{

	cout << "Snode identifier format: (my_id, snode_id, snode_size, gid of curr_negatiator)" << endl << endl;

	/*********************** INIT GLOBAL FILES/OBJECTS *************************/

	#pragma region

	vector<vector<int> > snode_snapshot;
	double tot_msg_send_sum, tot_cs_exit_wt;
	int n, k, temp = 0, completed_threads = 0, connected_threads = 0, thread_counter = 0;
	bool threads_done[max_threads]; //to keep track of which threads completed work

	#pragma endregion

	/*********************** INIT FILE STREAMS, GET PARAMS *********************/

	#pragma region

	string inp_parameters;
	ifstream input_file;
	fstream exp_file;

	input_file.open("inp-params.txt", ios::in);

	input_file >> n; //#threads
	input_file >> k; //#rounds
	
	#pragma endregion
	
	/***************************** INIT RANDOM GENERATOR ******************************/

	#pragma region

	srand(time(NULL)); //seed the random generator

	random_device rd; // obtain a random number from hardware
	mt19937 gen(rd());
	default_random_engine generator; //init generator

	omp_set_num_threads(n);
	
	#pragma endregion

#pragma omp parallel
	{
		/**************************** INIT THREAD LOCAL VARS ***************************/

		#pragma region

		int id = omp_get_thread_num();
		int server_sock_fd, last_fd, i, dest_thread;
		int curr_no_msg_sends = 0, curr_no_msg_rec = 0;

		double curr_time, drift_sleep, clock_drift = 0, new_drift = 0;
		char sender_buffer[max_buf_size];

		struct sockaddr_in server_addr;
		struct timeval tv = {1, 0}; //after 1 second select() will timeout

		fd_set all_fds, current_fds;

		// experiment stats
		long int tot_messages_sent = 0;
		long int tot_messages_sent_pr = 0;

		#pragma endregion

		/********************************* SOCKET CREATION ******************************/

		#pragma region

		//to prevent DDoSsing yourself by sequentially adding threads to server
		usleep(id * 100);

		if ((server_sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
			cout << "failed to create socket" << endl;
			exit(1);
		}

		#pragma endregion

		/******************************** INIT SERVER ADDR ******************************/

		#pragma region

		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(server_port);
		server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		memset(server_addr.sin_zero, '\0', sizeof server_addr.sin_zero);

		#pragma endregion

		/********************************** CONNECT TO SERVER ****************************/

		#pragma region

		if (connect(server_sock_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) < 0)
		{
			cout << "failed to connect to server" << endl;
			exit(1);
		}

		#pragma endregion

		/******************************* CONFIG THE FD BIT ARRAYS  *************************/

		#pragma region

		FD_ZERO(&all_fds);
		FD_ZERO(&current_fds);
		FD_SET(server_sock_fd, &all_fds); //set server fd to 1
		last_fd = server_sock_fd;		  //we only need to listen to the server port

		#pragma endregion

		/******************************	 CONFIG NODE AND SNODE DATA *********************************/

		#pragma region

		int left_channel, right_channel;
		if (id == 0) left_channel = n - 1; else left_channel = id - 1;
		if (id == n - 1) right_channel = 0; else right_channel = id + 1;

#pragma omp critical
		{
			connected_threads++;
		}

		// init super node of size 1 (only me and i am negotiator)
		struct my_supernode my_snode;
		my_snode.snode_id = rand() % 100000;
		my_snode.my_id = id; //this is used only by us. thread does not require knowledge of its id
		my_snode.n_threads = n;
		// my_snode.my_left_channel = left_channel;
		// my_snode.my_right_channel = right_channel;
		my_snode.snode_size = 1;
		my_snode.curr_round = 0;
		
		// my_snode.my_pos = 0;
		my_snode.curr_negotiator = id; // at round 1, every node is its own Snode and negotiator
		my_snode.curr_tail = id; // at round 1, every node is its own Snode, negotiator and tail

		my_snode.snode_password = rand() % 1000;
		my_snode.snode_private_encryp_key = rand() % 1000;

		while (connected_threads != n)
			;

		#pragma endregion

		while (true)
		{
			bool skip = false; 
			reset_snode_flags(my_snode); // reset all flags set during previous round 

			/**************** NEGOTIATOR SETS IT CURR_CLIENT******************/

			#pragma region

			if(my_snode.curr_negotiator == my_snode.my_id){

				my_snode.deal_accepted = false;

				if (my_snode.curr_round == 0) //if this is the 0th round then each node is its own snode and it is the negotiator.
				{
					//it choose who its curr client is i.e. its left/right channel
					int sample = rand() % 100;
					
					if (sample <= 60) 
					{
						my_snode.curr_client = left_channel; 
						my_snode.snode_direction = 1;
					}
					else {
						my_snode.curr_client = right_channel;
						my_snode.snode_direction = 0;
					}
					
				}
				
				else{ 
					if(my_snode.snode_direction) my_snode.curr_client = left_channel; else my_snode.curr_client = right_channel;
				}	
			}
						
			#pragma endregion

			/***************************** DISPLAY NODE-SNODE MAPPING  **********************************/

			#pragma region

#pragma omp barrier

			while (thread_counter != id)
				;

#pragma omp critical
			{

				cout << "Node: " << id << endl;
				display_snode(&my_snode, tot_messages_sent, tot_messages_sent_pr);
				tot_messages_sent_pr = 0;
				thread_counter++;
			}

#pragma omp barrier

			if (id == 0) thread_counter = 0;

			#pragma endregion

			/********************** NEGOTIATOR OF SNODE SENDS VOTE TO CURR CLIENT ******************/

			#pragma region

			if (my_snode.curr_negotiator == id)
			{

				//send the message to the dest server
				my_snode.message_type = 0;						//this implie this is a vote message
				my_snode.message_dest = my_snode.curr_client;				
				my_snode.next_snode_password = rand() % 1000; 
				my_snode.next_snode_private_encryp_key = rand() % 1000;

				string to_send = create_vote_message(my_snode);
				strcpy(sender_buffer, to_send.c_str());
			
				send(server_sock_fd, sender_buffer, strlen(sender_buffer), 0); // me -> server -> other clients
				tot_messages_sent_pr++;
				tot_messages_sent++;

#pragma omp critical
				{
					curr_time = preprocess_timestamp(omp_get_wtime());
					cout << "Snode (" << id << ", "<< my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") voted for ";
					cout << "Snode (" << my_snode.message_dest << ", "<< "?" << ", " << "?" << ", " << my_snode.curr_negotiator << ")" << endl;
					// cout << my_snode.curr_client << endl;
					// cout << my_snode.message_dest << endl;

				}
			}

			usleep(1000); //this is needed to ensure the message server has enough time to process and broadcast
			
			#pragma endregion

			/***************** NEGOTIATOR CHECKS FOR MUTUAL VOTE FROM CURR CLIENT ************************/

			#pragma region

			//update current set of fds, incase an fd has been added/ closed
			my_supernode recv_snode;

			current_fds = all_fds;

			if (my_snode.curr_negotiator == id)
			{ // check if i am negotiator

				//once an fd is set, condition will breaks
				if (select(last_fd + 1, &current_fds, NULL, NULL, &tv) < 0)
				{
					cout << "error occured during select" << endl;
					exit(1);
				}

				// some socket has pushed data
				if (FD_ISSET(server_sock_fd, &current_fds))
					service_socket(sender_buffer, server_sock_fd, my_snode, recv_snode);
			}

			#pragma endregion
			
			/***************** NEGOTIATORS CONFIG NEW MERGED SNODE DATA ************************/

			#pragma region

			my_supernode merged_snode = my_snode;

			//the two negotiators first config and update their merged snode data
			//the following properties must be configed: id, size, curr_neg, curr_tail, snode_direction
			if (merged_snode.deal_accepted && merged_snode.curr_negotiator == id)
			{

				#pragma omp critical
				{
					cout << "Snode (" << id << ", " << merged_snode.snode_id << ", " << merged_snode.snode_size << ", " << merged_snode.curr_negotiator << ") got mutual vote from ";
					cout << "Snode (" << recv_snode.my_id << ", " << recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ")" << endl;
				}
			
				// each merging snode will update its snode info depending on who had bigger snode_id
				if(merged_snode.snode_id == recv_snode.snode_id)
				{
					cout << "error. got the same snode id while trying to merge!" << endl;
					exit(0);
				}
				else if(merged_snode.snode_id < recv_snode.snode_id) //snode with smaller gid
				{
					merged_snode.curr_client = -1; //this is only know to curr_negotiator when voting occurs
					merged_snode.snode_size += recv_snode.snode_size; //update size
					merged_snode.snode_id = recv_snode.snode_id; //take gid of bigger snode
					merged_snode.curr_negotiator = recv_snode.curr_tail; //negotiator becomes the tail of the other snode
					merged_snode.curr_tail = merged_snode.curr_tail; //tail of merged snode is my tail

					// use the new password and key from other snode
					merged_snode.snode_password = recv_snode.next_snode_password;
					merged_snode.snode_private_encryp_key = recv_snode.next_snode_private_encryp_key;
				}
				else //snode with larger gid
				{
					merged_snode.curr_client = -1; //this is only know to curr_negotiator when voting occurs
					merged_snode.snode_size += recv_snode.snode_size; //update size
					merged_snode.curr_negotiator = merged_snode.curr_tail; //negotiator becomes tail of my snode
					merged_snode.snode_direction = !(merged_snode.snode_direction); //direction will flip 
					merged_snode.curr_tail = recv_snode.curr_tail; //tail of merged snode is tail of other snode

					// use the new password and key from my own snode
					merged_snode.snode_password = my_snode.next_snode_password;
					merged_snode.snode_private_encryp_key = my_snode.next_snode_private_encryp_key;
					
				}
				
			#pragma endregion
			}
				
			/*********** NEGOTIATORS OF MERGING SNODES BROADCASTS MERGED SNODE INFO TO ITS MEMBERS ************/

			#pragma region

			if(my_snode.curr_negotiator == id && my_snode.deal_accepted){
				
				if(my_snode.snode_size <= 1) //neg. has no other nodes to broadcast to
				{	
					noop
				}
				else
				{
					//configure and send a MERGE_SUCCESS message to the node opposite to curr_client
					merged_snode.message_type = 1; //indicated MERGE_SUCCESS message
					merged_snode.ttl = my_snode.snode_size - 1; //used as ttl
					
					//configure which neightbour curr_neg. has to send message to
					if(my_snode.snode_direction == 1) merged_snode.message_dest = right_channel; else merged_snode.message_dest = left_channel;

					//send the broadcast message
					string to_send = create_vote_message(merged_snode);
					strcpy(sender_buffer, to_send.c_str());
	
					send(server_sock_fd, sender_buffer, strlen(sender_buffer), 0); // me -> server -> other clients
					tot_messages_sent_pr++;
					tot_messages_sent++;

	#pragma omp critical
					{
						curr_time = preprocess_timestamp(omp_get_wtime());
						cout << "Snode (" << id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") sent MESSAGE_SUCCESS to " << merged_snode.message_dest << endl;
						
					}

				}

			}
			
			#pragma endregion

			/*********** NEGOTIATOR DEAL NOT ACCEPTED, BROADCAST MERGE_FAILED MESSAGE TO MEMBERS ****************/

			#pragma region
			
			if(my_snode.snode_size > 1 && my_snode.curr_negotiator == id && !my_snode.deal_accepted)
			{
				#pragma omp critical
				{
					cout << "Snode (" << id << ", " << merged_snode.snode_id << ", " << merged_snode.snode_size << ", " << merged_snode.curr_negotiator << ") FAILED TO MERGE in this round" << endl;
				}

				//configure which neightbour curr_neg. has to send message to
				if(my_snode.snode_direction == 1) my_snode.message_dest = right_channel; else my_snode.message_dest = left_channel;
 
				//configure and send a MERGE_FAIL message to the node opposite to curr_client
				my_snode.message_type = 2; //indicated MERGE_FAIL message
				my_snode.ttl = my_snode.snode_size - 1; //used as ttl

				//randomly flip a coin to decide if the curr_tail becomes the new negotiator
				int sample = rand() % 100;

				if(sample > 50)
				{
					my_snode.curr_negotiator = my_snode.curr_tail; // set the curr_neg as curr_tail
					my_snode.curr_tail = my_snode.my_id; // set the curr_tail as curr_neg
					my_snode.snode_direction = !my_snode.snode_direction; // invert direction

					skip = true; // to ensure that this node is not treated as a non-negotiator for the rest of this round

					#pragma omp critical
					{
						cout << "Snode (" << id << ", " << merged_snode.snode_id << ", " << merged_snode.snode_size << ", " << merged_snode.curr_negotiator << ") set curr_tail as new negotiator" << endl;
					}
				}
				
				//send the broadcast message
				string to_send = create_vote_message(my_snode);
				strcpy(sender_buffer, to_send.c_str());

				send(server_sock_fd, sender_buffer, strlen(sender_buffer), 0); // me -> server -> other clients
				tot_messages_sent++;
				tot_messages_sent_pr++;

				#pragma omp critical
				{
					curr_time = preprocess_timestamp(omp_get_wtime());
					cout << "Snode (" << id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") sent MESSAGE_FAIL to " << my_snode.message_dest << endl;
					
				}
				
			}

			#pragma endregion

			/***************** NEGOTIATORS UPDATE NEW MERGED SNODE DATA ************************/

			#pragma region
			
			if(my_snode.curr_negotiator == id && my_snode.deal_accepted)
			{
				my_snode = merged_snode;
				skip = true; //this variable is used to ensure that this node doesnt want for a merge_success/fail message in the next section
			}

			#pragma endregion
			
			/********************** NON-NEGOTIATOR CHECKS FOR TERMINATION *********************************/
			// #pragma region

			// if()
			// #pragma endregion

			/******* NON-NEGOTIATOR WAIT TILL THEY GET EITHER MERGE_SUCCESS OR MERGE_FAIL MESSAGE FROM NEG. ***********/

			#pragma region

			//update current set of fds, incase an fd has been added/ closed
			my_supernode recv_merged_snode;
			bool serviced = false;

			if(my_snode.curr_negotiator != id && my_snode.snode_size > 1 && !skip)
			{
				
				current_fds = all_fds;		
				
				while(true) // keep trying till i get merge status from curr_neg
				{	
					#pragma omp critical
					{
						cout << "thread: " << id << " is waiting for MERGE_SUCCESS/MERGE_FAIL" << endl;
					}
					
					if (select(last_fd + 1, &current_fds, NULL, NULL, &tv) < 0)
					{
						cout << "error occured during select" << endl;
						exit(1);
					}

					// some socket has pushed data
					if (FD_ISSET(server_sock_fd, &current_fds)){
						service_socket(sender_buffer, server_sock_fd, my_snode, recv_merged_snode);
						
					}

					// CHECK FOR TERMICNTION DETECTION

					if(recv_merged_snode.termination_detected){
						// send my_snode to the node who sent you a vote with same snode id and size
						my_snode.message_type = 3;
						if(my_snode.snode_direction == 1) my_snode.message_dest = right_channel; else my_snode.message_dest = left_channel;
						//serialise and send the message
						string to_send = create_vote_message(my_snode);
						strcpy(sender_buffer, to_send.c_str());
						send(server_sock_fd, sender_buffer, strlen(sender_buffer), 0); 
						tot_messages_sent_pr++;
						tot_messages_sent++;
						#pragma omp critical
							{
								cout << "Snode (" << id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator 
									<< ") sent TD check to: " << my_snode.message_dest << endl;
							}

						
						
					}
								
					// CHECK IF MERGE STATUS RECEIVED AND RELAY TO NEIGHBOR IF NEEDED

					if(my_snode.merge_status_received){

						recv_merged_snode.ttl -= 1; //decrement ttl by one

						// check whether we need to relay this message to any other nodes in the snode
						if(recv_merged_snode.ttl > 0){

							// ttl is already decremented, only message_dest needs to be changed acc. 
							// rest of merged_snode/recv_snode_2 is the same
							if(my_snode.snode_direction) recv_merged_snode.message_dest = right_channel; else recv_merged_snode.message_dest = left_channel;

							//serialise and send the message
							string to_send = create_vote_message(recv_merged_snode);
							strcpy(sender_buffer, to_send.c_str());
							send(server_sock_fd, sender_buffer, strlen(sender_buffer), 0); // me -> server -> other clients
							tot_messages_sent_pr++;
							tot_messages_sent++;
							
							#pragma omp critical
							{
								cout << "Snode (" << id << ", " << merged_snode.snode_id << ", " << merged_snode.snode_size << ", " << merged_snode.curr_negotiator 
									<< ") relayed merge status message to thread: " << recv_merged_snode.message_dest << endl;
							}
								
						}	

						// UPDATE SNODE DATA USING RECEIVED SNODE DATA 

						if(recv_merged_snode.message_type == 1){ // sucessfull merge, //update my snode to include details of merged snode
							my_snode.snode_id = recv_merged_snode.snode_id;
							my_snode.snode_size = recv_merged_snode.snode_size;
							my_snode.snode_direction = recv_merged_snode.snode_direction;

							my_snode.curr_negotiator = recv_merged_snode.curr_negotiator;
							my_snode.curr_tail = recv_merged_snode.curr_tail;

							my_snode.snode_password = recv_merged_snode.snode_password;
							my_snode.snode_private_encryp_key = recv_merged_snode.snode_private_encryp_key;

						}
						else if(recv_merged_snode.message_type == 2) { // no mutual vote, failed merge, snode may have changed direction so update just incase

							my_snode.curr_negotiator = recv_merged_snode.curr_negotiator;
							my_snode.curr_tail = recv_merged_snode.curr_tail;
							my_snode.snode_direction = recv_merged_snode.snode_direction;

						}
						break;

					}
					usleep(10000);
				}

			}

			#pragma endregion
			
			/******************************* BARRIER FOR NEXT ROUND *********************************/
			#pragma region

#pragma omp barrier
			if (id == 0)
			{
				
				cout << endl
						<< "------------------round: " << my_snode.curr_round << " over------------------" << endl
						<< endl;
			}
			my_snode.curr_round++;

#pragma omp barrier
			if (my_snode.curr_round == k) exit(0);
			// usleep(10000);

			#pragma endregion
		}
			
		/************************** DONE, WAIT FOR OTHER THREADS AND PRINT STATS *******************************/

		//barrier to wait till statistics are printed
		// while (temp != n);
		
	} //END OF CONCURRENT REGION

	return 0;
}
