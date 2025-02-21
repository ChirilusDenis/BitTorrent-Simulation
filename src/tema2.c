#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define SIZE1 0
#define SIZE2 1
#define NAME 2
#define ARRAY1 3
#define ARRAY2 4
#define INSTRUCTION 5
#define INSTRUCTION_UPLOAD 6
#define NUMBER_UPLOAD 7
#define NAME_UPLOAD 8

#define ACK -1
#define NACK -2
#define REQUEST_FILE -3
#define REQUEST_SEEDS -4
#define DONE -5
#define DOWNLOAD -6
#define CLOSE -7
#define REQUEST_NUM_CONEX -8


typedef struct f{
    char name[MAX_FILENAME];
    int num_seg;
    char hashes[MAX_CHUNKS][HASH_SIZE + 1];
} file;

int num_mine;
int initial_num_mine;
file my_files[MAX_FILES];
int num_wanted;
char wanted[20][MAX_FILENAME];

typedef struct ft {
    char name[MAX_FILENAME];
    int num_seg;
    char hashes[MAX_CHUNKS][HASH_SIZE + 1];
    int num_seeds;
    int seeds[10];
    int num_peers;
    int peers[10];
} tracker_file;
int num_files;

tracker_file t_files[MAX_FILES];

int connections;

// Function to find a file index in the tracker's database
int find_file(char *name) {
    int found = -1;
    for(int i = 0; i < num_files; i++)
        if(!strncmp(name, t_files[i].name, MAX_FILENAME)) found = i;
    return found;
}

// A function that return the host with the least number of connections,
// along with that number ,from a given host pool
int find_min_conex(int *hosts, int num_hosts, int my_rank, int *nr) {
    int min = 9999;
    int best = -1;
    int msg;
    for(int i = 0; i < num_hosts; i++) {
        msg = REQUEST_NUM_CONEX;
        if(hosts[i] != my_rank) {
            MPI_Send(&msg ,1 , MPI_INT , hosts[i] , INSTRUCTION_UPLOAD , MPI_COMM_WORLD);
            MPI_Recv(&msg , 1 , MPI_INT , hosts[i] , NUMBER_UPLOAD , MPI_COMM_WORLD , MPI_STATUS_IGNORE);

            if(msg < min) {
                min = msg;
                best = hosts[i];
            }
        }
    }
    (*nr) = min;
    return best;
}

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    int type;
    int num_seeds;
    int seeds[10];
    int num_peers;
    int peers[10];
    int num_seg;
    FILE *out;

    for(int i = 0; i < num_wanted; i++) {
        
        // Compose the name of the downloaded file
        char new_name[50];
        new_name[0] = '\0';
        strcat(new_name, "client");
        sprintf(new_name + 6, "%d", rank);
        strcat(new_name, "_");
        strcat(new_name, wanted[i]);
        out = fopen(new_name, "w");

        // Send the action code and the name of the missing file
        // Receive the number of segments and all the hashes
        int type = REQUEST_FILE;
        MPI_Send(&type , 1 , MPI_INT , TRACKER_RANK , INSTRUCTION , MPI_COMM_WORLD);
        MPI_Send(wanted[i] , MAX_FILENAME , MPI_CHAR , TRACKER_RANK , NAME , MPI_COMM_WORLD);

        MPI_Recv(&num_seg , 1 , MPI_INT , TRACKER_RANK , SIZE1 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
        MPI_Recv(my_files[num_mine].hashes , num_seg * (HASH_SIZE + 1) , MPI_CHAR , TRACKER_RANK ,
            ARRAY1 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
        strncpy(my_files[num_mine].name, wanted[i], MAX_FILENAME);
        num_mine++;

        for(int j = 0; j < num_seg; j++) {
            if(j % 10 == 0) {
                // Ask the tracker for a list of seeders and peers for a certain file
                type = REQUEST_SEEDS;
                MPI_Send(&type , 1 , MPI_INT , TRACKER_RANK , INSTRUCTION , MPI_COMM_WORLD);
                MPI_Send(wanted[i] , MAX_FILENAME , MPI_CHAR , TRACKER_RANK , NAME , MPI_COMM_WORLD);

                MPI_Recv(&num_seeds , 1 , MPI_INT , TRACKER_RANK , SIZE1 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
                MPI_Recv(seeds , num_seeds , MPI_INT , TRACKER_RANK , ARRAY1 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
                MPI_Recv(&num_peers , 1 , MPI_INT , TRACKER_RANK , SIZE2 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
                MPI_Recv(peers , num_peers , MPI_INT , TRACKER_RANK , ARRAY2 , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
            }

            // Choose the least busy host from the swarm
            int best_peer_conex;
            int best_peer = find_min_conex(peers, num_peers, rank, &best_peer_conex);

            int best_seed_conex;
            int best_seed = find_min_conex(seeds, num_seeds, rank, &best_seed_conex);
            
            int host;
            if(best_peer == -1) host = best_seed;
            else if(best_peer_conex < best_seed_conex) host = best_peer;
            else host = best_seed;

            // Send request for a certain numbered segment
            int msg;
            MPI_Send(&j , 1 , MPI_INT , host , INSTRUCTION_UPLOAD , MPI_COMM_WORLD);
            MPI_Send(wanted[i] , MAX_FILENAME , MPI_CHAR , host , NAME_UPLOAD , MPI_COMM_WORLD);
            MPI_Recv(&msg , 1 , MPI_INT , host , INSTRUCTION , MPI_COMM_WORLD , MPI_STATUS_IGNORE);

            if(msg == ACK) {
                my_files[num_mine - 1].num_seg++;
            } else {
                host = best_seed;
                MPI_Send(&j , 1 , MPI_INT , host , INSTRUCTION_UPLOAD , MPI_COMM_WORLD);
                MPI_Send(wanted[i] , MAX_FILENAME , MPI_CHAR , host , NAME_UPLOAD , MPI_COMM_WORLD);
                MPI_Recv(&msg , 1 , MPI_INT , host , INSTRUCTION , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
                
                if(msg == ACK) {
                    my_files[num_mine - 1].num_seg++;
                } else printf("NO HOST.\n");
            }
        }
        
        // Write the downloaded file
        for(int j = 0; j < my_files[num_mine - 1].num_seg; j++) {
            fprintf(out, "%s", my_files[num_mine - 1].hashes[j]);
            if(j != num_seg - 1) fprintf(out, "\n");
        }

        // Signal to the tracker that the file has finished downloading
        type = DOWNLOAD;
        MPI_Send(&type , 1 , MPI_INT , TRACKER_RANK , INSTRUCTION , MPI_COMM_WORLD);
        MPI_Send(wanted[i] , MAX_FILENAME , MPI_CHAR , TRACKER_RANK , NAME , MPI_COMM_WORLD);
    }

    // Signal to the tracker that all files have finished downloaded
    type = DONE;
    MPI_Send(&type , 1 , MPI_INT , TRACKER_RANK , INSTRUCTION , MPI_COMM_WORLD);
    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    MPI_Status stat;
    char buff[50];

    int type;
    while(1) {
        // Receive action code
        MPI_Recv(&type , 1 , MPI_INT , MPI_ANY_SOURCE , INSTRUCTION_UPLOAD , MPI_COMM_WORLD , &stat);
        int client = stat.MPI_SOURCE;
        if(type == CLOSE) break;

        else if(type == REQUEST_NUM_CONEX) {
            // Return the requested number of connections
            type = connections;
            MPI_Send(&type, 1 , MPI_INT , client , NUMBER_UPLOAD , MPI_COMM_WORLD);
        } 

        else {
            // Send the requested sgment, given by segment number and file name
            MPI_Recv(buff , MAX_FILENAME , MPI_CHAR , client , NAME_UPLOAD , MPI_COMM_WORLD , MPI_STATUS_IGNORE);

            int found = -1;
            for(int i = 0; i < num_mine; i++) {
                if(!strncmp(buff, my_files[i].name, MAX_FILENAME)) {
                    if(my_files[i].num_seg > type) {
                        found = i;
                        type = ACK;
                        connections++;
                        MPI_Send(&type , 1 , MPI_INT , client , INSTRUCTION , MPI_COMM_WORLD);
                        break;
                    } 
                }
            }
            if(found == -1) {
                type = NACK;
                MPI_Send(&type , 1 , MPI_INT , client , INSTRUCTION , MPI_COMM_WORLD);
            }
        }
    }

    return NULL;
}

void tracker(int numtasks, int rank) {
    MPI_Status stat;
    char buff[50];
    int write_to;
    int nseg;

    // Get the data of all the files on the network
    for(int i = 1; i < numtasks; i++) {
        int num;
        MPI_Recv(&num , 1, MPI_INT , i , SIZE1 , MPI_COMM_WORLD ,&stat);
        for(int j = 0; j < num; j++) {
            MPI_Recv(buff , MAX_FILENAME , MPI_CHAR , i , NAME , MPI_COMM_WORLD , &stat);
            write_to = find_file(buff);
            if(write_to == -1) write_to = num_files++;

            strncpy(t_files[write_to].name, buff, MAX_FILENAME);
            t_files[write_to].seeds[t_files[write_to].num_seeds++] = i;

            MPI_Recv(&nseg , 1 , MPI_INT , i , SIZE2 , MPI_COMM_WORLD , &stat);
            MPI_Recv(t_files[write_to].hashes , (HASH_SIZE + 1) * nseg , MPI_CHAR , i , ARRAY1 , MPI_COMM_WORLD, &stat);
            t_files[write_to].num_seg = nseg;
        }
    }

    // Sginal clients to start uploading/downloading
    int msg = ACK;
    for(int i = 1; i < numtasks; i++) {
        MPI_Send(&msg, 1 , MPI_INT , i , INSTRUCTION , MPI_COMM_WORLD);
    }

    int type;
    int client;
    int done = 0; 
    while(done != numtasks - 1) {
        // Receive request type
        MPI_Recv(&type , 1 , MPI_INT , MPI_ANY_SOURCE , INSTRUCTION , MPI_COMM_WORLD , &stat);
        client = stat.MPI_SOURCE;

        if(type == REQUEST_FILE) {
            // Send back data about a certain file, requested by file name
            MPI_Recv(buff , MAX_FILENAME , MPI_CHAR , client , NAME , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
            int nfile = find_file(buff);
            t_files[nfile].peers[t_files[nfile].num_peers++] = client; 

            MPI_Send(&t_files[nfile].num_seg , 1 , MPI_INT , client , SIZE1 , MPI_COMM_WORLD);
            MPI_Send(t_files[nfile].hashes , (HASH_SIZE + 1) * t_files[nfile].num_seg ,
                MPI_CHAR , client , ARRAY1 , MPI_COMM_WORLD);
        }

        else if(type == REQUEST_SEEDS) {
            // Send the seeders and peers of a file, requested by file name
            MPI_Recv(buff , MAX_FILENAME , MPI_CHAR , client , NAME , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
            int nfile = find_file(buff);

            MPI_Send(&t_files[nfile].num_seeds , 1 , MPI_INT , client , SIZE1 , MPI_COMM_WORLD);
            MPI_Send(t_files[nfile].seeds , t_files[nfile].num_seeds , MPI_INT , client , ARRAY1 , MPI_COMM_WORLD);
            MPI_Send(&t_files[nfile].num_peers , 1 , MPI_INT , client , SIZE2 , MPI_COMM_WORLD);
            MPI_Send(t_files[nfile].peers , t_files[nfile].num_peers , MPI_INT , client , ARRAY2 , MPI_COMM_WORLD);
        }
        
        else if(type == DOWNLOAD) {
            // Receive signal that a client finished downloading a file, given by name
            MPI_Recv(buff , MAX_FILENAME , MPI_CHAR , client , NAME , MPI_COMM_WORLD , MPI_STATUS_IGNORE);
            int nfile = find_file(buff);
            int found = 0;
            for(int i = 0; i < t_files[nfile].num_peers - found; i++) {
                if(t_files[nfile].peers[i] == client) found = 1;
                if(found) t_files[nfile].peers[i] = t_files[nfile].peers[i + 1]; 
            }
            t_files[nfile].num_peers--;
            t_files[nfile].peers[t_files[nfile].num_peers] = 0;

            t_files[nfile].seeds[t_files[nfile].num_seeds++] = client;
        }

        else if(type == DONE) {
            // Received signal that a client finished downloading everything
            done++;
        }
    }

    // Send signals to close upload thread, sent to all clients
    type = CLOSE;
    for(int i = 1; i < numtasks; i++) {
        MPI_Send(&type , 1 , MPI_INT , i , INSTRUCTION_UPLOAD , MPI_COMM_WORLD);
    }

}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    char filename[15] = "inx.txt";
    filename[2] = rank + 48;
    

    FILE *input = fopen(filename, "r");

    // Read my files
    fscanf(input, "%d", &num_mine);
    initial_num_mine = num_mine;
    for(int i = 0 ; i < num_mine; i++) {
        fscanf(input, "%s %d", &my_files[i].name, &my_files[i].num_seg);
        for(int j = 0; j < my_files[i].num_seg; j++) {
            fscanf(input, "%s", my_files[i].hashes[j]);
        }
    }

    fscanf(input, "%d", &num_wanted);
    for(int i = 0; i < num_wanted; i++) {
        fscanf(input, "%s", wanted[i]);
    }

    // Send all my files' data to the tracker
    MPI_Send(&num_mine , 1 , MPI_INT , TRACKER_RANK , SIZE1 , MPI_COMM_WORLD);
    for(int i = 0; i < num_mine; i++) {
        MPI_Send(my_files[i].name , MAX_FILENAME , MPI_CHAR , TRACKER_RANK , NAME , MPI_COMM_WORLD);
        MPI_Send(&my_files[i].num_seg , 1 , MPI_INT , TRACKER_RANK , SIZE2 , MPI_COMM_WORLD);
        MPI_Send(my_files[i].hashes , (HASH_SIZE + 1) * my_files[i].num_seg , MPI_CHAR , TRACKER_RANK , ARRAY1 , MPI_COMM_WORLD);
    }
    int from_tracker;
    MPI_Status stat;
    MPI_Recv(&from_tracker , 1 , MPI_INT , TRACKER_RANK , INSTRUCTION , MPI_COMM_WORLD , &stat);


    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
