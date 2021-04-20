#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <list>
#include <netinet/in.h>
#include <omp.h>
#include <random>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <chrono>


using namespace std;


#define max_threads 20
#define max_buf_size 5120
#define max_k 20
#define noop

//function declarations

struct my_supernode;
double preprocess_timestamp(double time);

using uint32 = unsigned int;




struct my_supernode //this ds is used by all nodes to keep track of its own and its snodes metadata.
{

    // my own metadata
    int n_threads = -1;        //number of threads in the ring (this is only for our use, nodes do not know n)
    int my_id = -1;            //my global id (this is for our use only, nodes do not know each others ids)
    // int my_left_channel = -1;  //gid of left neighbour
    // int my_right_channel = -1; //gid of rigth neighbour

    // my snode metadata
    int snode_id = -1;        //this is randomly generated so it does not uniquely identify an snode
    int snode_size = 1;         // number of nodes present
    int snode_direction = -1; //the direction from curr_neg to curr_tail, 0->left, 1->right
    // int my_pos = -1;          //what is my relative position in the Snode i.e. 0, 1, 2 .. snode_size-1
    int curr_negotiator = -1; //global id of the current negotiator in the Snode
    int curr_tail = -1;         //global id of the current tail in the Snode
    int curr_client = -1;     // the gid of the node that curr_neg. will vote for next round. //important: curr_client is only known by curr_negotiator of the snode and will be set when vote is sent
    int curr_round = 0;

    // status flags used in parse_received_messages()
    bool deal_accepted = false; //used to see if snodes curr_neg got a mutual vote from its curr_client
    bool merge_status_received = false; 
    bool termination_detected = false;
    
    // message metadata
    int message_type = 0; // check format in the below comment
    int message_dest = -1; // the destination gid of the node the message is for
    int ttl = -1; //this is used by member to check if message came from inside snode

    
    /* SNode Authenticationn and encryption password exchange for termination detection

        - a common snode password and private key is maintained by every member of the snode. 
        - during merging of 2 snodes the new password and key is broadcasted to all members.
        - the raw password or private key is never shared to any other external snode.
        - it can be used to assert that a particular node belongs to an snode.

    - Why is this authentication needed?

        - During the final roundn when only one snode (the entire ring) is remaining, the head will persistly be forced to vote for its own tail
        - The tail on seeing this needs to detect that its own head is voting for it and therefore signal the terminationn of the algorithm.
        - There is no foolproof/deterministic way for the tail to distinguish if the head that is voting comes from its own snode or another snode.
        - The only option left is using authenntication, where the tail of the snode can authennticate itself to the head.
        - All authenntication by nature however is still Monte carlo. An adversarial node can try and crack a password.

    - Method for authentication of Tail:

        1. The tail on seeing a vote coming from a head node with a) the same snode id b) the same snode size, will execute check_completition as follows:
        2. It creates encrypted password using snode_password and snode_private_encryption key.
        3. It sends this ecnrypted pass as a "TD check" type message back to the voting node.
        4. The head on seeing this will decrypt the password using its own copy of the private key.
        5. If the password matches its own, the tail is successfully authenticated i.e. it belongs to the head own snode.
        6. it the elects itself as leader and broadcasts this info to its snode (the entire ring)
    
    */
    int snode_password = -1;
    int snode_private_encryp_key = -1; 
    int snode_encryp_password = -1; 

    int next_snode_password = -1;
    int next_snode_private_encryp_key = -1; 

    /* message_type mapping

    0 -> VOTE         
        Sender: the negotiator of one Snode 
        Dest: the chosen adjacent Snode 
        Action: used for voting between negotiators of Snodes

    1 -> MERGE_SUCCESS 
        Sender: the negotiator of one Snode
        Dest: all members of the Snode
        Action: merge succeed, members should get and update details of merged snode

    2 -> MERGE_FAILED
        Sender: the negotiator of one Snode
        Dest: all members of the Snode
        Action: used to tell snode members that mutual vote wasnt received

    3 -> MERGE_SUCCESS 
        Sender: the negotiator of one Snode
        Dest: all members of the Snode
        Action: merge succeed, members should get and update details of merged snode
    */
};



void display_snode(my_supernode *my_snode){
    
    cout << "\t snode_id: " << my_snode->snode_id << endl;
    cout << "\t snode_size: " << my_snode->snode_size << endl;
    if(my_snode->snode_direction) cout << "\t snode_direction: right" << endl;
    else cout << "\t snode_direction: left" << endl;
    cout << "\t curr negotiator: " << my_snode->curr_negotiator << endl;
    cout << "\t curr tail: " << my_snode->curr_tail << endl;
    if(my_snode->curr_client == -1)
        cout << "\t curr client: ?" << endl;
    else
        cout << "\t curr client: " << my_snode->curr_client << endl;
    cout << "\t snode password: " << my_snode->snode_password << endl;
    cout << "\t snode private key: " << my_snode->snode_private_encryp_key << endl << endl;
        
}

string create_vote_message(my_supernode s)
{

    std::stringstream ss;

    ss << ' ' << s.n_threads 
        << ' ' << s.my_id; 
        // << ' ' << s.my_left_channel 
        // << ' ' << s.my_right_channel;
    ss << ' ' << s.snode_id 
        << ' ' << s.snode_size 
        << ' ' << s.snode_direction 
        // << ' ' << s.my_pos 
        << ' ' << s.curr_negotiator;
    ss << ' ' << s.curr_tail 
        << ' ' << s.curr_client 
        << ' ' << s.curr_round 
        << ' ' << s.deal_accepted 
        << ' ' << s.merge_status_received
        << ' ' << s.termination_detected
        << ' ' << s.message_type 
        << ' ' << s.message_dest 
        << ' ' << s.ttl
        << ' ' << s.snode_password 
        << ' ' << s.snode_private_encryp_key
        << ' ' << s.snode_encryp_password
        << ' ' << s.next_snode_password 
        << ' ' << s.next_snode_private_encryp_key; 

     

    return ss.str();
}

void parse_single_vote(my_supernode &recv_snode, char message_recv[], int base = 0)
{
    stringstream ss(message_recv + base);

    ss >> recv_snode.n_threads;
    ss >> recv_snode.my_id;
    // ss >> recv_snode.my_left_channel;
    // ss >> recv_snode.my_right_channel;
    ss >> recv_snode.snode_id;
    ss >> recv_snode.snode_size;
    ss >> recv_snode.snode_direction;
    // ss >> recv_snode.my_pos;
    ss >> recv_snode.curr_negotiator;
    ss >> recv_snode.curr_tail;
    ss >> recv_snode.curr_client;
    ss >> recv_snode.curr_round;
    ss >> recv_snode.deal_accepted;
    ss >> recv_snode.merge_status_received;
    ss >> recv_snode.termination_detected;
    ss >> recv_snode.message_type;
    ss >> recv_snode.message_dest;
    ss >> recv_snode.ttl;
    ss >> recv_snode.snode_password;
    ss >> recv_snode.snode_private_encryp_key;
    ss >> recv_snode.snode_encryp_password;
    ss >> recv_snode.next_snode_password;
    ss >> recv_snode.next_snode_private_encryp_key;
}

void parse_received_votes(char message_recv[], int server_sock_fd, my_supernode &my_snode, my_supernode &recv_snode)
{
    // the negotiators of all Snodes will parse their messages to check and see if they got a mutual vote using this function

    stringstream ss(message_recv);

    while (true)
    {
        if (ss.peek() == ss.eof())
        {
            cout << "something went wrong..." << endl;
            exit(0);
        }

        //check if there are no more messages to parse
        if (ss >> recv_snode.n_threads) noop; else return;

        ss >> recv_snode.my_id;
        // ss >> recv_snode.my_left_channel;
        // ss >> recv_snode.my_right_channel;
        ss >> recv_snode.snode_id;
        ss >> recv_snode.snode_size;
        ss >> recv_snode.snode_direction;
        // ss >> recv_snode.my_pos;
        ss >> recv_snode.curr_negotiator;
        ss >> recv_snode.curr_tail;
        ss >> recv_snode.curr_client;
        ss >> recv_snode.curr_round;
        ss >> recv_snode.deal_accepted;
        ss >> recv_snode.merge_status_received;
        ss >> recv_snode.termination_detected;
        ss >> recv_snode.message_type;
        ss >> recv_snode.message_dest;
        ss >> recv_snode.ttl;
        ss >> recv_snode.snode_password;
        ss >> recv_snode.snode_private_encryp_key;
        ss >> recv_snode.snode_encryp_password;
        ss >> recv_snode.next_snode_password;
        ss >> recv_snode.next_snode_private_encryp_key;

        
    
        if (recv_snode.message_dest == my_snode.my_id && (recv_snode.curr_round == my_snode.curr_round || recv_snode.message_type == 3)) //check if message is for me and it is not old message
        {
            //display received vote
            #pragma omp critical
            {   
                cout << "Snode (" << my_snode.my_id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") got vote from ";
                cout << "Snode (" << recv_snode.my_id << ", "<< recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ")" << endl;
                // cout << ". (global time: " << preprocess_timestamp(omp_get_wtime()) << ")." << endl;
            }
            
            // (MONTE CARLO) tail got vote from some negotiator, check to see if it came from its own snode
            if (recv_snode.message_type == 0 && my_snode.my_id == my_snode.curr_tail) 
            {
                noop
                if(recv_snode.snode_id == my_snode.snode_id && recv_snode.snode_size == my_snode.snode_size){
                     #pragma omp critical
                        {   
                            cout << "*** Snode (" << my_snode.my_id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") detected TERMINATION" << endl;
                            // cout << "Snode (" << recv_snode.my_id << ", "<< recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ") ***" << endl;
                            // cout << ". (global time: " << preprocess_timestamp(omp_get_wtime()) << ")." << endl;
                        }
                }

                recv_snode.termination_detected = true;
                // return;
            }

            // negotiator checks to see if it got a td check auth request from tail
            if(recv_snode.message_type == 3){

                #pragma omp critical
                {   
                    cout << "Snode ("<< my_snode.my_id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") got TD message from ";
                    cout << "Snode ("<< recv_snode.my_id << ", " << recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ")" << endl;

                    if(recv_snode.snode_password == my_snode.snode_password){
                        cout << "TERMINATION DETECTED! Leader is: " << recv_snode.my_id << endl;
                        exit(0);
                    
                    }
                        
                }
               
            }

            if (recv_snode.message_type == 0 && recv_snode.curr_negotiator == my_snode.curr_client) // check to see if i got a mutual vote
            {
                my_snode.deal_accepted = true;
                return;
            }

            if(recv_snode.message_type == 1){
                #pragma omp critical
                {   
                    cout << "Snode ("<< my_snode.my_id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") got MERGE_SUCCESS from ";
                    cout << "Snode ("<< recv_snode.my_id << ", " << recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ")" << endl;
                    // cout << ". (global time: " << preprocess_timestamp(omp_get_wtime()) << ")." << endl;
                }
                my_snode.merge_status_received = true;
                
                return;
                
            }

            if(recv_snode.message_type == 2){

                #pragma omp critical
                {   
                    cout << "Snode ("<< my_snode.my_id << ", " << my_snode.snode_id << ", " << my_snode.snode_size << ", " << my_snode.curr_negotiator << ") got MERGE_FAIL from ";
                    cout << "Snode ("<< recv_snode.my_id << ", " << recv_snode.snode_id << ", " << recv_snode.snode_size << ", " << recv_snode.curr_negotiator << ")" << endl;
                    // cout << ". (global time: " << preprocess_timestamp(omp_get_wtime()) << ")." << endl;
                }
                my_snode.merge_status_received = true;
                
                return;
                
            }

        }
    }
}

/******************** utility functions ********************/

bool check_completion(bool *threads_done, int n_threads)
{

    for (int i = 0; i < n_threads; i++)
        if (!threads_done[i])
            return false;

    return true;
}

double preprocess_timestamp(double time)
{
    //truncates the leading 8 digits in the timestamp for clarity
    double base = (double)((int)time / 1000) * 1000; //truncates the 10s, 100s, 1000s place and floating points to 0
    return (time - base);
}

void reset_snode_flags(my_supernode &my_snode){
    // used to reset all transient flags after a round is over

    my_snode.deal_accepted = false;
    my_snode.merge_status_received = false;
    my_snode.termination_detected = false;

    my_snode.message_type = -1;
    my_snode.message_dest = -1;
    my_snode.ttl = -1;

}

