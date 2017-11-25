#include "Moonder.h"

//Global Variables
//Circular Linked List (CLL) node structure to store client info
//Head node has PID = -1
//HEAD--->-1--->PID1--->PID2--->...PIDi<---LAST

typedef struct my_CLLNode{		
	int PID;
	int AssignArrIdx;
	char name[MAXNAMELEN];
	char info[MAXMSGLEN];
	struct my_CLLNode *Next, *Prev;
}CLLNode;
//shared variables
CLLNode *Head, *Last;

typedef struct dataStruct{
	int clQID;
	CLLNode *clNodePtr;
}dStruct;

// AssignArr[] stores the PID of clients currently logged on
int AssignArr[MAXNUMCLIENTS], AssignArrCount;
// LikeMatrix[][] keeps track of clients who like each other, referenced from AssignArr[]
char LikeMatrix[MAXNUMCLIENTS][MAXNUMCLIENTS];
int joinQID;	
int retval, clientCount;
pthread_t thread_id[MAXNUMCLIENTS];
sem_t *editClientSem;		//used to enforce mut-ex server/client threads access the (3) data structures

void printClientsFwd(void);

//-------------------------Server Thread functions (START)-------------------------//
void insertCLL(CLLNode *client){
	client->Next = Head;
	client->Prev = Last;
	Last->Next = client;
	Head->Prev = client;
	Last = client;
}

void removeCLL(CLLNode *current){
	CLLNode *temp;
	if(Head == Last);		//if there are no clients, do nothing
	else{
//		temp = current->Prev;
		current->Next->Prev = current->Prev;
		current->Prev->Next = current->Next;
		//have to update LAST here
		if (Last == current) Last = current->Prev;
	}
}

//initClientQueue() here is the same as initClientQueue() in Client.c
int initClientQueue(char *filename, int PID){
	key_t key;
	int clientQID;
	if ((key = ftok(filename, PID)) == -1) {
		perror("ftok");
		return(-1);
	}
	if ((clientQID = msgget(key, 0666 | IPC_CREAT)) == -1) {
		fprintf(stderr, "Server: Cannot create/join Client Queue.\n");
		perror("msgget");
		return(-1);
	}
	return clientQID;
}

dStruct* initClient(JOINMsg *msgPtr){
	CLLNode *clientNode;
	int clientQID;		//msg queue ID of private server-client queue
	int AArrayIdx;
	dStruct *dS;

	clientNode = malloc( sizeof(CLLNode));		//create new CLL node
	clientNode->PID = msgPtr->PID;
	strcpy(clientNode->name, msgPtr->mname);
	strcpy(clientNode->info, msgPtr->minfo);
	clientNode->Next = clientNode->Prev = NULL;
	//semaphore ON
	if( sem_wait(editClientSem) == -1){ 
		perror("sem_wait");
		exit(EXIT_FAILURE);
	}
	insertCLL(clientNode);		//insert node
	clientNode->AssignArrIdx = msgPtr->AAIdx;		//assign client PID to next empty space in AArray
	clientQID = initClientQueue("ftokfile.txt", msgPtr->PID);
	//semaphore OFF
	if( sem_post(editClientSem) == -1 ){
		perror("sem_post");
		exit(EXIT_FAILURE);
	}
printf("ClientQID is %d, msgPtr->PID is %d\n", clientQID, msgPtr->PID);	
	dS = malloc(sizeof(dStruct));
	dS->clQID = clientQID;
	dS->clNodePtr = clientNode;
	return dS;
}

//Remove client (update linked list, deallocate resources), clear assignment array and like matrix of client 
int removeClient(int QID, CLLNode *node){
	int i;
	
	msgctl(QID, IPC_RMID, NULL);
	/////semaphore ON
	if( sem_wait(editClientSem) == -1){ 
		perror("sem_wait");
		return 1;
	}
	for(i=0; i<MAXNUMCLIENTS; i++)
			LikeMatrix[node->AssignArrIdx][i] = LikeMatrix[i][node->AssignArrIdx] = EMPTYCHAR;
	removeCLL(node);
	free(node);
	/////semaphore OFF
	if( sem_post(editClientSem) == -1 ){
		perror("sem_post");
		return 1;
	}
	return 0;
}

void setStatus(CLIENTMsg* cMsg, CLLNode* clientCLL, CLLNode* threadCLLNext){
	cMsg->status = 0x0000;
	if(LikeMatrix[clientCLL->AssignArrIdx][threadCLLNext->AssignArrIdx] == 'L')		//if client likes candidate
		cMsg->status = cMsg->status | 0x0001;
	if(LikeMatrix[threadCLLNext->AssignArrIdx][clientCLL->AssignArrIdx] == 'L') 	//if candidate likes client
		cMsg->status = cMsg->status | 0x0002;
}

//copies info from node to msg, and sets command type to COMM
void setMsg(int COMM, CLIENTMsg *msg, CLLNode *node){
	msg->mtype = COMM;
	msg->PID = node->PID;
	strcpy(msg->mname, node->name);
	strcpy(msg->minfo, node->info);
}

void *ping(void *data){
	//requires: clientPID, clientQID, 
	int clientPingQID, clientQID;
	CLIENTMsg msg;
	JOINMsg *jMsgPtr = data;
	//create/join (separate) message queue of client (uses pingfile.txt as hash for key)
	clientPingQID = initClientQueue("pingfile.txt", jMsgPtr->PID);
	msg.mtype = PING;
	msg.PID = jMsgPtr->PID;
	strcpy(msg.mname, jMsgPtr->mname);
	strcpy(msg.minfo, jMsgPtr->minfo);
	while(1){		//sends PING messages every 1 ms
		if (msgsnd(clientPingQID, &msg, sizeof(CLIENTMsg)-sizeof(long), 0) == -1){
			fprintf(stderr, "Server: error- cannot send PING message to client ping MQ: %d ,", msg.PID);
			fprintf(stderr, "closing ping()\n");
			perror("msgsnd");
			break;
		}
		sleep(0.1);
		if (msgrcv(clientPingQID, &msg, sizeof(CLIENTMsg)-sizeof(long), 0, IPC_NOWAIT) == -1){
			fprintf(stderr, "Server: error - client %d disconnected.\n", jMsgPtr->PID);
			perror("msgrcv");
			break;
		}
	}	//END while(1) loop
	clientQID = initClientQueue("ftokfile.txt", jMsgPtr->PID);
	msg.mtype = DISCONNECTED;
	msg.PID = jMsgPtr->PID;
	msgsnd(clientQID, &msg, sizeof(CLIENTMsg)-sizeof(long), 0);
	msgctl(clientPingQID, IPC_RMID, NULL);		//remove Ping MQ 
	pthread_exit(NULL);

}

//server thread, one per client
void *serverThread(void *data){
	int candidateQID;	//msg queue ID of candidateQID (for CHAT)
	int clientQID;		//msg queue ID of private server-client queue
	CLLNode *clientCLL, *threadCLLNext;		//pointer to next candidate in client thread
	JOINMsg *jMsgPtr = data, jMsg;
	CLIENTMsg cMsg, canMsg, tempMsg;
	dStruct *dBuffer;
	int retval;
	pthread_t pingThread;
	//create private semaphore between server thread and client 'sem+PID', w/init value = 0
	char semname[MAXNAMELEN];
	sem_t *tSemPtr; 

	jMsg.PID = jMsgPtr->PID;
	jMsg.AAIdx = jMsgPtr->AAIdx;
	strcpy(jMsg.mname, jMsgPtr->mname);
	strcpy(jMsg.minfo, jMsgPtr->minfo);
	
	dBuffer = initClient(&jMsg);
	clientQID = dBuffer->clQID;
	clientCLL = dBuffer->clNodePtr;
	threadCLLNext = Head;		//initialize current candidate to Head of CLL

	sprintf(semname, "/tSem%d", jMsg.PID); 
	umask(0x0000);
	tSemPtr = sem_open(semname, O_CREAT, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH, 0);
	if (tSemPtr == SEM_FAILED){ perror("sem_open"); }
	
	printClientsFwd();

	setMsg(ACK, &cMsg, threadCLLNext);
	
	//send ACK message to client
	if( msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0) == -1){
		fprintf(stderr, "Server: cannot send ACK message to client PID: %d\n", cMsg.PID);
		perror("msgsnd");
	}
	printf("ACK message sent to client\n");
	//-----semwait(sem+PID) here
	//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
	sleep(1);
	
//create (sub)thread to "PING" client to see if connection is still good. Set timeout of about 1 ms.
/*
	retval = pthread_create(&pingThread, NULL, ping, (void*)jMsgPtr);
	if( retval ){
		fprintf(stderr, "\n Server: error(%d): cannot create connection pinger ping()\n", retval);
		perror("pthread_create");
	}
*/
	cMsg.mtype = PASS;
	while (cMsg.mtype != QUIT) { /* serverThread listens for commands from client*/

		if (clientCount == 1){ 		//only one (self) client logged on, override command type in message
			cMsg.mtype = NIL;
			cMsg.PID = -1;
			cMsg.status = NOTLIKED;
			strcpy(cMsg.mname, threadCLLNext->name);
			strcpy(cMsg.minfo, threadCLLNext->info);
		}
		
		printf("client %d mtype is %ld\n", clientCLL->PID, cMsg.mtype);
		switch (cMsg.mtype){
			case PASS: 
				printf("PASS command received\n");
				threadCLLNext = threadCLLNext->Next;	//check linked list for the next candidate
				while (threadCLLNext->PID == -1 || threadCLLNext->PID == jMsg.PID)	
					threadCLLNext = threadCLLNext->Next;	//advance ptr to next if Head or same as client
				//determine status of next candidate and set status bits
				setStatus(&cMsg, clientCLL, threadCLLNext);
				printf("In PASS, cMsg.status=%d, cMsg.PID=%d, cMsg.mname=%s,\n cMsg.minfo=%s\n", cMsg.status, cMsg.PID, cMsg.mname, cMsg.minfo);
				setMsg(OK, &cMsg, threadCLLNext);
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);	//send info to client 
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				break;
			case ENDCHAT:	//ENDCHAT command can come from either client - procedure similar to PASS, can combine
				threadCLLNext = threadCLLNext->Next;	//check linked list for the next candidate
				while (threadCLLNext->PID == -1 || threadCLLNext->PID == jMsg.PID)	
					threadCLLNext = threadCLLNext->Next;	//advance ptr to next if Head or same as client
				//determine status of next candidate and set status bits
				setStatus(&cMsg, clientCLL, threadCLLNext);
				setMsg(OK, &cMsg, threadCLLNext);
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);	//send info to client 
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				break;
			case LIKE:
				printf("LIKE command received\n");
				//update client's Like Matrix against current candidate to like.
				LikeMatrix[clientCLL->AssignArrIdx][threadCLLNext->AssignArrIdx] = 'L';
				//---rest same as PASS
				threadCLLNext = threadCLLNext->Next;	//check linked list for the next candidate
				while (threadCLLNext->PID == -1 || threadCLLNext->PID == jMsg.PID)	
					threadCLLNext = threadCLLNext->Next;	//advance ptr to next if Head or same as client
				//determine status of next candidate and set status bits
				setStatus(&cMsg, clientCLL, threadCLLNext);
				setMsg(OK, &cMsg, threadCLLNext);
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);	//send info to client			
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				break;
			case CHAT:	//CHAT command originates from client (single starting point)
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);//relays back CHAT message to originator client
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				//determine candidateQID of current candidate (threadCLLNext) to be able to send message to that client MQ
				candidateQID = initClientQueue("ftokfile.txt", threadCLLNext->PID);
				printf("CHAT command received, clientQID is %d, candidateQID is %d\n", clientQID, candidateQID);
				canMsg.status = 0x0003;		//status is MATCHED
				setMsg(CHATCAN, &canMsg, clientCLL);	//send canMsg to current candidate to CHATCAN
				msgsnd(candidateQID, &canMsg, sizeof(CLIENTMsg)-sizeof(long), 0); 
				//client should send ENDCHAT command if client ends chat
				//-----NONONONO semwait(sem+PID) here
				break;
			case CHATCAN:
//to be able to synch properly, this recipient server thread must read (ahead) the next message from its client in the MQ and store the command. 
//Commands can be PASS, LIKE, QUIT, CHAT, ENDCHAT, (or another CHATCAN)
//After the CHATCAN msg is sent to the client, the stored command is processed. 
//Alternatively, the command from the client can be discarded. This will simplify the processing by quite a bit... In this way, the message synch is maintained
				if (msgrcv(clientQID, &tempMsg, sizeof(CLIENTMsg)-sizeof(long), 0, 0) == -1) perror("msgrcv");
//As you can see, handling this case can be quite complex!!!!!
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);//relays back CHAT message to candidate client
				//-----semwait(sem+PID) here, since received an extra msg from chat originator candidate
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				break;
			case NIL:
				printf("NIL command received\n");
				msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);			
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); }
				threadCLLNext = threadCLLNext->Next;	//check linked list for the next candidate
				if (threadCLLNext->PID == -1)	threadCLLNext = threadCLLNext->Next;	//advance ptr to next if Head
				break;
			case QUIT:		//same as DISCONNECTED:, removeClient() is called after exit from while() loop
			case DISCONNECTED:
				break;
			default: 
				fprintf(stderr, "Server: error - invalid command from client %d\n", cMsg.PID);
				//should disconnect client and close server thread (to recover and save server's state)
				break;
		}//END switch()

		if (msgrcv(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0, 0) == -1) {
			perror("msgrcv");
		}
		//-----sempost(sem+PID) here
		//if( sem_post(tSemPtr) < 0){ perror("sem_post"); }
	}//END while (cMsg.mtype != QUIT) loop

//thread closing procedures (QUIT command received from client)
	removeClient(clientQID, clientCLL);				
	//pthread_join(pingThread, NULL);
	//send UNJOIN message to Join MQ so server can free up space (pthread_join) in thread array, and decrease the client count.
	jMsg.mtype = UNJOIN;
	printf("UNJOIN sent to server parent. Client PID is %d\n", jMsg.PID);
	//jMsgPtr->PID = same;
	msgsnd(joinQID, &jMsg, sizeof(JOINMsg)-sizeof(long), 0); 
	
	pthread_exit(NULL);
}
//-------------------------Server Thread functions (END)-------------------------//


//-------------------------Server Parent functions (START)-------------------------//
int initServer(void){
	int i;

	editClientSem = sem_open("/serverSem", O_CREAT, S_IRUSR|S_IWUSR, 1);
	if (editClientSem == SEM_FAILED) {
		fprintf(stderr, "Server: failed to create semaphore\n");
		perror("sem_open");
		return 1;
	}

	clientCount =0;		//number of clients = 0 @ start
	for(i=0; i< MAXNUMCLIENTS; i++){		//initialize AssignArr[] and LikeMatrix[][]
		AssignArr[i] = EMPTYPID;
		memset(LikeMatrix[i], EMPTYCHAR, MAXNUMCLIENTS);
	}		
	CLLNode *HeadNode;
	HeadNode = malloc(sizeof(CLLNode));
	HeadNode->PID = -1;
	HeadNode->AssignArrIdx = -1;
	HeadNode->Prev = HeadNode->Next = HeadNode;
	Head = Last = HeadNode;		//initialize CLL nodes to NULL
	//create JOIN message queue
	if ((joinQID = msgget(JOINKEY, 0666 | IPC_CREAT)) == -1) {
		fprintf(stderr, "Server startup error: unable to create JOIN queue.\n");
		perror("msgget");
		return(1);
	}
	return 0;
}

int assignAArray(int PID){
	int i=0;
	while(AssignArr[i] != EMPTYPID && i<MAXNUMCLIENTS)	i++;
	AssignArr[i] = PID;
	return i;
}

int findAArray(int PID){
	int i=0;
	while(AssignArr[i] != PID && i<MAXNUMCLIENTS) i++;
	return i;
}

void monitorJoinQueue(void){
	//spawn a thread "joiner" (resource deallocator/recycler) to join client threads that have ended
	//this will keep track of the next empty thread index (by checking the Assignment Array)	
	int found;
	//checks JOIN queue for new clients
	for(;;) { /* Server never quits! */
		JOINMsg joinMsg;		//create an instance of joinMsg for every new client
//		found = 0;		//usused variable
		if (msgrcv(joinQID, &joinMsg, sizeof(JOINMsg)-sizeof(long), 0, 0) == -1) {
			perror("msgrcv");
		}
		printf("PID: \"%d\"\n", joinMsg.PID);
		printf("Name: \"%s\"\n", joinMsg.mname);
		printf("Info: \"%s\"\n", joinMsg.minfo);
		
		//create new thread for client
		if(joinMsg.mtype == JOIN){
			if (clientCount == MAXNUMCLIENTS){
				fprintf(stderr, "Server: maximum number of clients logged on.\n");
				continue;
			}
			joinMsg.AAIdx = assignAArray(joinMsg.PID);
			retval = pthread_create(&thread_id[joinMsg.AAIdx], NULL, serverThread, (void*)(&joinMsg));
			if( retval ){
				perror("pthread_join");
				fprintf(stderr, "\n Server: ERROR - return code from pthread_create is %d \n", retval);
			}
			clientCount++;
			printf("Client PID:%d joined, clientCount is now %d, AA index is %d\n", joinMsg.PID, clientCount, joinMsg.AAIdx); 
		}
		else if(joinMsg.mtype == UNJOIN){
			joinMsg.AAIdx = findAArray(joinMsg.PID);
			AssignArr[joinMsg.AAIdx] = EMPTYPID;
			retval = pthread_join(thread_id[joinMsg.AAIdx], NULL);
			if( retval ){
				perror("pthread_join");
				fprintf(stderr, "\n Server: ERROR - return code from pthread_join is %d \n", retval);
			}
			clientCount--;
			printf("Client PID:%d UNjoined, clientCount is now %d, AA index is %d\n", joinMsg.PID, clientCount, joinMsg.AAIdx); 
			printClientsFwd();
		}
	}
}

int shutdownServer(void){
	int retval;

	retval = sem_close(editClientSem);
	if (retval == -1){
		fprintf(stderr, "Server: error - semaphore failed to close\n");
		perror("sem_close");
	}
  printf("Server: deallocating all message queue(s).\n");
	//call all thread processes to exit;
	if (msgctl(joinQID, IPC_RMID, NULL) == -1) {
		perror("msgctl");
		return(1);
	}
	return 0;
}
//-------------------------Server Parent functions (END)-------------------------//

void signalHandler(int sigNum) {
	char response='\0';
  if (sigNum == SIGINT){
  	fprintf(stdout, "This action will shutdown the server process. ");
  	while (tolower(response) != 'y'){
			fprintf(stdout, "Are you sure you want to quit? (Y/n)");
			fscanf(stdin, "%c", &response);
			if (tolower(response) == 'n') return;
		}
		shutdownServer();
		printf("Server: process exited successfully.\n");
		exit(0);
		//use atexit() or on_exit()
	}
}

//Test printing clients logged on
void printClientsFwd(void){
	CLLNode *temp;
	int count = 0;
	temp = Head->Next;
	printf("Head: ");
//	while(temp->PID != -1){
	while(temp != Head){
		count++;
		printf("%d: PID:%d,Aidx:%d -> ", count, temp->PID, temp->AssignArrIdx);
		temp = temp->Next;
	}
	printf("END\n");
}

int main(void){

	int i, j;
	__sighandler_t ret;
	//JOINMsg joinMsg;

	initServer();
/*
	//test contents of AssignArr[] and LikeMatrix[][]
	for(i=0; i<MAXNUMCLIENTS; i++){
		printf("AssignArr[%d] = %d ", i, AssignArr[i]);
		printf("LikeMatrix[%d]: ", i);
		for(j=0; j<MAXNUMCLIENTS; j++)
			printf("\"%c\", ", LikeMatrix[i][j]); 
		printf("\n");
	}		
*/
	printf("Press ^C to quit server process.\n");
	ret = signal(SIGINT, signalHandler);
	if (ret == SIG_ERR){
		fprintf(stderr,"Oops--SIGINT cannot be caught\n");
		exit(1);
	}
	
	monitorJoinQueue();
	
	return 0;
}
