#include "Moonder.h"

//Global variables
int joinQID, clientQID, chatQID, pingQID; //NOTE: clientQID points to the same private queue as clientQID in Server.c
JOINMsg joinMsg;
CLIENTMsg clientMsg, pingMsg;
CHATMsg chatMsg;
pthread_t pingThread;
//create private semaphore between server thread and client 'sem+PID', w/init value = 0
char semname[MAXNAMELEN];
sem_t *tSemPtr; 

void initVariables(void){
	joinQID = clientQID = chatQID = pingQID = -1;
	joinMsg.mtype = clientMsg.mtype = pingMsg.mtype = chatMsg.mtype = 0;
	joinMsg.PID = clientMsg.PID = pingMsg.PID = chatMsg.PID = -1;
	joinMsg.AAIdx = -1;
	clientMsg.status = pingMsg.status = 0;
	strcpy(clientMsg.mname, "\0");
	strcpy(clientMsg.minfo, "\0");
	strcpy(pingMsg.mname, "\0");
	strcpy(pingMsg.minfo, "\0");
	strcpy(chatMsg.mname, "\0");
	strcpy(chatMsg.minfo, "\0");
}

//initClientQueue() here is the same as initClientQueue() in Server.c
int initClientQueue(int PID){
	key_t key;
	if ((key = ftok("ftokfile.txt", PID)) == -1) {
		perror("ftok");
		return(1);
	}
	if ((clientQID = msgget(key, 0666 | IPC_CREAT)) == -1) {
		fprintf(stderr, "Client: Cannot create/join Client Queue.\n");
		perror("msgget");
		return(1);
	}
	return 0;
}

int initPingQueue(int PID){
	key_t key;
	if ((key = ftok("pingfile.txt", PID)) == -1) {
		perror("ftok");
		return(1);
	}
	if ((pingQID = msgget(key, 0666 | IPC_CREAT)) == -1) {
		fprintf(stderr, "Client: Cannot create/join Client Queue.\n");
		perror("msgget");
		return(1);
	}
	return 0;
}

int initJoinQueue(void){
	joinMsg.mtype = JOIN; /* we don't really care in this case */
	joinMsg.PID = getpid();
	//connect to JOIN queue
	if ((joinQID = msgget(JOINKEY, 0666 )) == -1) { 
		fprintf(stderr, "Client: startup error. unable to connect to JOIN queue.\n");
		perror("msgget");
		return(1);
	}
	return 0;
}

int joinJoinQueue(void){
	//send message to server JOIN queue
	if ((msgsnd(joinQID, &joinMsg, sizeof(JOINMsg)-sizeof(long), 0)) == -1) { 
		fprintf(stderr, "Client: initialization error - unable to send messages to server JOIN queue.\n");
		perror("msgsnd");
		return(1);
	}
	fprintf(stdout, "Client: request sent to join Moonder server joinQueue.\n");
	return 0;
}

int initClient(void){

	initVariables();
	if( initJoinQueue() ) return 1;
	if( joinJoinQueue() ) return 1;

	if( initClientQueue(joinMsg.PID)) return 1;		//create client queue first
printf("ClientQID is %d, PID is %d\n", clientQID, joinMsg.PID);	
	
	fprintf(stdout, "Client: joined Moonder server clientQueue.\n");
	return 0;
}

void *ping (void *data){
	CLIENTMsg cMsg;
	initPingQueue(joinMsg.PID);
	while(1){		//sends PING messages every 1 ms
		if (msgrcv(pingQID, &pingMsg, sizeof(CLIENTMsg)-sizeof(long), 0, 0) == -1){
			fprintf(stderr, "Client: error - server %d disconnected.\n", joinMsg.PID);
			fprintf(stderr, "closing ping()\n");
			perror("msgrcv");
			break;
		}
		else {
			if (msgsnd(pingQID, &pingMsg, sizeof(CLIENTMsg)-sizeof(long), 0) == -1){
				fprintf(stderr, "Client: cannot send PING message to server ping MQ: %d ,", joinMsg.PID);
				fprintf(stderr, "closing ping)\n");
				perror("msgsnd");
				break;
			}
		}
	}	//END while(1) loop
	
	cMsg.mtype = DISCONNECTED;
	cMsg.PID = joinMsg.PID;
	msgsnd(clientQID, &cMsg, sizeof(CLIENTMsg)-sizeof(long), 0);
	msgctl(pingQID, IPC_RMID, NULL);		//remove Ping MQ 
	pthread_exit(NULL);
	
}

void showClientData(CLIENTMsg *msg){
		printf("--------------------------------------------------------------------------\n");
		printf("%s\n", clientMsg.mname);
		printf("%s\n", clientMsg.minfo);	
}

char showSelection(char *string){
	int stringLen=strlen(string), i, valid = 0;
	int bufLen;
	char buf[255], ch;
	
//printf("stringLen = %d\n", stringLen);
	
	while( !valid){
		i=0;
		printf("Enter Selection (");
		while( string[i] != '\0'){		//while( i<stringLen ){
			printf("%c", string[i]);
			if(i<stringLen-1) 
				printf(",");
			i++;
		}
		printf("): ");
		fgets(buf, sizeof(buf), stdin);
	  bufLen = strlen(buf);
//printf("buf is %s, bufLen = %d\n", buf, bufLen);
		if (buf[bufLen-1] == '\n'){
			buf[bufLen-1] = '\0';	// ditch newline at end, if it exists
			bufLen--;
		}
		if (bufLen != 1 || strchr(string, buf[0]) == NULL) printf("Invalid response.\n");
		else valid = 1;
	}
	
	return buf[0];
}

void sendCMsg(char c){
	switch(c){
		case 'P':
			clientMsg.mtype = PASS;
			break;
		case 'L':
			clientMsg.mtype = LIKE;
			break;
		case 'C':
			clientMsg.mtype = CHAT;
			break;
		case 'Q':
			clientMsg.mtype = QUIT;
			break;	
	}
	msgsnd(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 0);
}

//initChatQueue() 
int initChatQueue(int PID){
	key_t key;
	if ((key = ftok("chatfile.txt", PID)) == -1) {
		perror("ftok");
		return(1);
	}
	if ((chatQID = msgget(key, 0666 | IPC_CREAT)) == -1) {
		fprintf(stderr, "Client: Cannot create/join Client Queue.\n");
		perror("msgget");
		return(1);
	}
	return 0;
}

int originatorChat(void){
	int sLen, closeMQ=0;
	//create private chat semaphore between server thread and client 'sem+chatPID', w/init value = 0
	if( initChatQueue(joinMsg.PID) ) return 1;
	printf("--------------------------------------------------------------------------\n");
	printf("in originatorChat(), after initChatQueue(), chatQID is %d\n", chatQID);
	while( 1 ){
		printf("You: ");
		chatMsg.mtype = CHAT;
		chatMsg.PID = clientMsg.PID;
		strcpy(chatMsg.mname, clientMsg.mname);
		//strcpy(chatMsg.minfo, clientMsg.minfo);
		fgets(chatMsg.minfo, sizeof(char)*MAXMSGLEN, stdin);
	  sLen = strlen(chatMsg.minfo);
		if (chatMsg.minfo[sLen-1] == '\n') chatMsg.minfo[sLen-1] = '\0';//ditch newline at end, if exists
		if( !strcmp("Q", chatMsg.minfo) ){	//if originator ends chat with 'Q'
			//send END command to recipient chat MQ
			msgsnd(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0);
			closeMQ=0;	//we're first to leave, so don't close the MQ
			break;
		}
		else{	//send message to server CHAT queue
			if (msgsnd(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0) == -1) { 
				fprintf(stderr, "Client: initialization error - unable to send messages to server CHAT queue.\n");
				perror("msgsnd");
				return 1;
			}
			//-----semwait(sem+chatPID) here
		}
		printf("%s: ", chatMsg.mname);
		msgrcv(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0, 0); 
		//-----sempost(sem+chatPID) here
		if(!strcmp("Q", chatMsg.minfo)){	//if recipient ends chat with 'Q'
			printf("%s ENDED CHAT\n", chatMsg.mname);
			//close chat MQ (the last client who received "Q" is the one to close it
			closeMQ=1;
			break;
		}
		else
			printf("%s\n", chatMsg.minfo);
	}	//end while( 1 ){ loop

	if(closeMQ){
		//close chat MQ and deallocate resources
		if (msgctl(chatQID, IPC_RMID, NULL) == -1) {
			perror("msgctl");
			return 1;
		}
	}
	//remove semaphore 'sem+chatPID'
	return 0;
}

int recipientChat(void){
	int endChat = 0, sLen, closeMQ=0;
	initChatQueue(clientMsg.PID);
	printf("--------------------------------------------------------------------------\n");
	printf("in recipientChat(), after initChatQueue(), chatQID is %d\n", chatQID);
	printf("CHAT FROM %s\n", clientMsg.mname);

	msgrcv(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0, 0); 
	//-----sempost(sem+chatPID) here
	if(!strcmp("Q", chatMsg.minfo)){	//if originator ends chat with 'Q'
		printf("%s ENDED CHAT\n", chatMsg.mname);
		//close chat MQ (the last client who received "Q" is the one to close it
		closeMQ=1;	//we're last, so close the MQ before exiting
		endChat = 1;
	}
	else
		printf("%s: %s\n", chatMsg.mname, chatMsg.minfo);

	while( !endChat ){
		printf("You: ");
		chatMsg.mtype = CHAT;
		chatMsg.PID = clientMsg.PID;
		strcpy(chatMsg.mname, clientMsg.mname);
		//strcpy(chatMsg.minfo, clientMsg.minfo);
		fgets(chatMsg.minfo, sizeof(char)*MAXMSGLEN, stdin);	//TO BE CONTINUED: wrong check for NULL ???
	  sLen = strlen(chatMsg.minfo);
		if (chatMsg.minfo[sLen-1] == '\n') chatMsg.minfo[sLen-1] = '\0';	// ditch newline at end, if it exists
		if( !strcmp("Q", chatMsg.minfo) ){	//if recipient ends chat with 'Q'
			//send END command to recipient chat MQ
			msgsnd(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0);
			closeMQ=0;
			endChat = 1;
			break;
		}
		else{	//send message to server CHAT queue
			if (msgsnd(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0) == -1) { 
				fprintf(stderr, "Client: initialization error - unable to send messages to server CHAT queue.\n");
				perror("msgsnd");
				return 1;
			}
			//-----semwait(sem+chatPID) here
		}
		printf("%s: ", chatMsg.mname);
		msgrcv(chatQID, &chatMsg, sizeof(CHATMsg)-sizeof(long), 0, 0); 
		//-----sempost(sem+chatPID) here
		if(!strcmp("Q", chatMsg.minfo)){	//if originator ends chat with 'Q'
			printf("%s ENDED CHAT\n", chatMsg.mname);
			//close chat MQ (the last client who received "Q" is the one to close it
			closeMQ=1;
			endChat = 1;
			break;
		}
		else
			printf("%s\n", chatMsg.mname, chatMsg.minfo);
	}	//end while( !endChat ){ loop

	if(closeMQ){
		//close chat MQ and deallocate resources
		if (msgctl(chatQID, IPC_RMID, NULL) == -1) {
			perror("msgctl");
			return 1;
		}
	}
	return 0;
}

int shutdownClient(void){
	clientMsg.mtype = QUIT;	//sends QUIT (last message) to client MQ
	msgsnd(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 0);//send message with selection/mtype 
/*
	printf("Client: deallocating all message queue(s).\n");
	if (msgctl(clientQID, IPC_RMID, NULL) == -1) {
		perror("msgctl");
		return(1);
	}
*/
	return 0;
}

int viewCandidates(void){
	int retval;
	char selection;
	//create private semaphore between server thread and client 'sem+PID', w/init value = 0
	sprintf(semname, "/tSem%d", joinMsg.PID); 
	umask(0x0000);
	tSemPtr = sem_open(semname, O_CREAT, S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH, 0);
	if (tSemPtr == SEM_FAILED){ perror("sem_open"); return 1; }

//wait for ACK(1) message from server thread (sent to clientQueue), then proceeds to Candidate Loop below
	if ( msgrcv(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 1, 0) == -1){
		fprintf(stderr, "Client: error (@264) - cannot receive messages from server.\n");
		perror("msgrcv");
		return 1;
	}
	if( clientMsg.mtype != ACK){
		fprintf(stderr, "Client: error - ACK message not received, synchronization error. mtype is %ld\n", clientMsg.mtype);
		return 1;
	}
	//-----sempost(sem+PID) here
	//if( sem_post(tSemPtr) < 0){ perror("sem_post"); return 1; }

	// create thread to ping (msgsnd and msgrcv)
/*
	retval = pthread_create(&pingThread, NULL, ping, NULL);
	if( retval ){
		fprintf(stderr, "\n CLient: error(%d): cannot create connection pinger ping()\n", retval);
		perror("pthread_create");
	}
*/
	selection = EMPTYCHAR;
	printf("Welcome to Moonder, %s. Perhaps you want to know: \n", joinMsg.mname);
	while ( selection != 'Q' ) {

		if ( msgrcv(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 0, 0) == -1){
			fprintf(stderr, "Client: error - cannot receive messages from server.\n");
			perror("msgrcv");
			return 1;
		}
		//-----sempost(sem+PID) here
		//if( sem_post(tSemPtr) < 0){ perror("sem_post"); return 1; }
		
		//printf("msgstatus: %d\n" + clientMsg.status);
		// if not matched
		switch (clientMsg.mtype){
			case OK:
				showClientData(&clientMsg);					//show client data
				printf("Case OK, clientMsg.status is %u\n", clientMsg.status);
				if(clientMsg.status == NOTLIKED)		//determine status of candidate
					selection = showSelection("PLQ"); //show PLQ (inputs that are not in selection)
				else if(clientMsg.status == LIKED){
					printf("LIKED\n");								//show "LIKED"
					selection = showSelection("PQ");	//show PQ
				}
				else if(clientMsg.status == MATCHED){
					printf("MATCHED\n");							//show "MATCHED"
					selection = showSelection("PCQ");	//show PCQ
				}
				else if(clientMsg.status == CANLIKED)		//determine status of candidate
					selection = showSelection("PLQ"); //show PLQ (inputs that are not in selection)
				else{																//ERROR case
					printf("ERROR\n");
					selection = showSelection("PQ");
				}
				sendCMsg(selection);								//send appropriate message to server
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); return 1; }
				break;
			case NIL:
				printf("--------------------------------------------------------------------------\n");
				printf("There are no other clients logged on at this time.\n");
				printf("Press \"P\" to refresh.\n");
				printf("Case NIL, clientMsg.status is %u\n", clientMsg.status);
				selection = showSelection("PQ");
				sendCMsg(selection);								//send appropriate message to server
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); return 1; }
				break; 
			case CHAT:		//when client selects 'C', server sends CHAT in response
				printf("Case CHAT, clientMsg.status is %u\n", clientMsg.status);
				//originator client uses his own PID to create MQ key
				originatorChat();
				//create CHAT MQ, stays in this function/state until 'Q' received in CHAT MQ, then returns here
				//send ENDCHAT msg to client MQ
				clientMsg.mtype = ENDCHAT;
				msgsnd(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 0);//send message with selection/mtype ENDCHAT
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); return 1; }
				break;
			case CHATCAN:		//when another candidate wants to initiate chat
				printf("Case CHATCAN, clientMsg.status is %u\n", clientMsg.status);
	   		recipientChat();
	   		clientMsg.mtype = PASS;	//ENDCHAT;
	   		msgsnd(clientQID, &clientMsg, sizeof(CLIENTMsg)-sizeof(long), 0);//send message with selection/mtype ENDCHAT
				//-----semwait(sem+PID) here
				//if( sem_wait(tSemPtr) < 0){ perror("sem_wait"); return 1; }
				break;
//			case ENDCHATCAN:	//when another candidate ends the chat first
//				break;
			case DISCONNECTED:	//DISCONNECTED received from ping()
				selection = 'Q';
				break;
			default:
				fprintf(stderr, "Client %d: error - invalid command from server\n", clientMsg.PID);
				break;
		}	//END of switch() statement
		
		
	}	//END of while() loop
   
	//deallocated MQs, other resources, close thread before exit (msgsnd QUIT signal)
	if( shutdownClient()) return 1;
//	pthread_join(pingThread, NULL);

	return 0;
}

void signalHandler(int sigNum) {
	char response='\0';
  if (sigNum == SIGINT){
  	fprintf(stdout, "This action will shutdown the Moonder client. ");
  	while (tolower(response) !='y'){
			fprintf(stdout, "Are you sure you want to quit? (Y/n)");
			fscanf(stdin, "%c", &response);
			if (tolower(response) == 'n') return;
		}
		shutdownClient();
		printf("Client: Moonder exited successfully.\n");
		exit(0);
	}
}

//TUI for Client.c
int main(void){
	__sighandler_t ret;
	char filename[MAXFILENAME];
	FILE *filePtr;
	int stringLen;
	char ch;
  
	printf("^_^ Welcome to Moonder client. ^_^\n");
	printf("Please enter your filename: ");

//	fscanf(stdin, "%s", filename);
//	while( (ch=fgetc(stdin)) != EOF); //clearing stdin buffer
//	while(!feof(stdin)) ch=fgetc(stdin);

	fgets(filename, MAXFILENAME-1, stdin);
  stringLen = strlen(filename);
	if (filename[stringLen-1] == '\n') filename[stringLen-1] = '\0';	// ditch newline at end, if it exists
printf("after fgets, filename is %s.\n", filename);	
	filePtr = fopen(filename, "r");
	fgets(joinMsg.mname, sizeof(char)*MAXNAMELEN, filePtr);
  stringLen = strlen(joinMsg.mname);
	if (joinMsg.mname[stringLen-1] == '\n') joinMsg.mname[stringLen-1] = '\0';	// ditch newline at end, if it exists
	fgets(joinMsg.minfo, sizeof(char)*MAXMSGLEN, filePtr);
  stringLen = strlen(joinMsg.minfo);
	if (joinMsg.minfo[stringLen-1] == '\n') joinMsg.minfo[stringLen-1] = '\0';	// ditch newline at end, if it exists
	fclose(filePtr);

	printf("Thank you. Please wait...\n");

	//initialize client-server private queue
	initClient();
	fprintf(stdout, "Client: connected to Moonder server.\n");
   
	printf("Press ^C to quit Moonder client\n");
	ret = signal(SIGINT, signalHandler);
	if (ret == SIG_ERR){
		fprintf(stderr,"Oops--SIGINT cannot be caught\n");
		exit(1);
	}

	//interact with Moonder server
  viewCandidates();
  printf("Client process exited successfully.\n");
	return 0;

}
