#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

#define MAXFILENAME 256
#define MAXMSGLEN 256
#define MAXNAMELEN 64
#define MAXNUMCLIENTS 8 //128
#define EMPTYPID -1
#define EMPTYCHAR 'Z'	//'\0'
#define JOINKEY 5001

#define MAXCHATCHARS 160		//max number of message characters in a message

//Command constants
#define ACK						1			//S-C
#define PING					2			//S-C
#define JOIN 					3			//C-S
#define	UNJOIN				4			//send UNJOIN to main server process when a server/client thread QUITs. 
#define PASS					5			//C-S(view next candidate)
#define LIKE					6			//C-S
#define CHAT					7			//CHAT command originates from client
#define	ENDCHAT				8			//ENDCHAT command originates from client
#define	CHATCAN				9			//CHATCAN command sent to candidate
#define	ENDCHATCAN		10			//ENDCHATCAN command originates from candidate
#define QUIT					11		//C-S
#define	DISCONNECTED	12		//S-C
#define	NIL						13		//S-C (no other clients logged on)
#define OK						14		//S-C return message from server (same as ACK, but in middle of candidate loop

//status bits (unsigned int): 0x0000
//0x000X (only uses last 4-bit): (other 4-bit(s) - reserved for future expansion)	

//client status code
//can't use this since CLIENTMsg.status is unsigned int
//#define	NOTAVAILABLE	-1		//Not a valid client - used with the NIL command where no other clients are logged on
//use NOTLIKED for cases associated with NIL command
#define NOTLIKED			0			//0x0000 - not liked (default)
#define LIKED					1			//0x0001 - liked (client)
#define CANLIKED			2			//0x0002 - candidate liked
#define MATCHED				3			//0x0003 - matched

//Message structure (same for all message queues?)
typedef struct my_JoinMsg {
	long mtype;
	int PID, AAIdx;
	char mname[MAXNAMELEN];
	char minfo[MAXMSGLEN];
}JOINMsg;

typedef struct my_ClientMsg {
	long mtype;
	int PID;
	unsigned int status;		//status bits - determines notliked, liked, or match(both liked)
	char mname[MAXNAMELEN];
	char minfo[MAXMSGLEN];
}CLIENTMsg;

typedef struct my_CHATMsg {
	long mtype;
	int PID;
	char mname[MAXNAMELEN];
	char minfo[MAXMSGLEN];
}CHATMsg;


