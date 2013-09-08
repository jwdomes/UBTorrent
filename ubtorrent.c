#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "bencode.h"
#include "common_headers.h"
#include "error_handlers.h"
#include "sha1.h"
#include "robust_io.h"
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BITFIELDLEN 16
#define DLULPRECISION 10
#define backlog 1024
#define MAXCONNECTEDCLIENTS 5
#define MAXPOTENTIALPEERS 5
#define MESSAGEDIGESTSIZE 20
#define FiveTwelve 512
#define BitFieldLength 10000
#define BitFieldLengthuint8 1250
#define SixteenK 16384
#define TORRENTLENGTH 32768
#define TwoFiftySixK 262144
#define STARTTRACKER 0
#define STOPTRACKER 1
#define COMPLETETRACKER 2

#define CHOKE 0
#define UNCHOKE 1
#define INTERESTED 2
#define NOTINTERESTED 3
#define HAVE 4
#define BITFIELD 5
#define REQUEST 6
#define PIECE 7
#define CANCEL 8


#ifndef max
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#define MAXNUMPIECES 80

// SIGPIPE
struct sigaction new_action;

//Global Variables
int numPieces = 0;
int lastPieceSize = 0;
int totalDownload = 0;
int totalUpload = 0;
int totalLeft = 0;
double bufferToByteSize = 0;
const int on = 1;
int HostNametoIPAddressStatus = 0;
int isSeeder = 0;
pthread_mutex_t mutexArgs = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexConnectedClients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexResetClients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexPeerConnected = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexBitfieldUpdate = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexPieceRequest = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexUpdateDL = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexUpdateUL = PTHREAD_MUTEX_INITIALIZER;

//TCP Variables
int trackerFD = 0;
int trackerPort = 0;
struct sockaddr_in trackerAddress;

//Threads
pthread_t announceThread;
pthread_t acceptThread;
pthread_t workerThreads[MAXCONNECTEDCLIENTS];
pthread_t optimisticThread;

//https://computing.llnl.gov/tutorials/pthreads/
pthread_mutex_t readMutex;
pthread_mutex_t writeMutex;

int8_t pieceField[BitFieldLength] = {-1}; /* The piece field is initally set to -1 to indicate that a particular piece should not be counted
							   A.K.A if the torrent file has 5 pieces then the 6th and on will be -1. */
int thisBitField[BitFieldLength];

int TCP_Listening_FD = 0; /* This current client's TCP listending socket FD*/
struct sockaddr_in thisClientAddress; /* The current client's address */
int thisPort = 0;		/* The port that the app is running on */
char inputFile[256] = {'\0'};
char thisIP[INET_ADDRSTRLEN] = {'\0'};
int  torrentFileInt[TORRENTLENGTH] = {'\0'};	/* The torrent file in Integer representation */
char torrentFile[TORRENTLENGTH] = {'\0'};	/* The torrent file in char representation */
char consoleInput[TORRENTLENGTH] = {'\0'};	/* The console input in char representation */

uint8_t     myID_hash[20];  /* The Generated MyID Hash */

struct TorrentInformation metaInformation;
struct TorrentInformation
{
	char announce[FiveTwelve];
	int CreationDate;
	uint8_t     info_hash[MESSAGEDIGESTSIZE];  /* The info dictionary hash in char representation */
	int file_info_length;
	char info_meta_file_name[FiveTwelve];
	char  info_name[FiveTwelve];
	int info_piece_length;
	int bigHash[TORRENTLENGTH];
	int bigHashLength;
};
struct TrackerInformation trackerInfo;
struct TrackerInformation
{
	int complete;
	int downloaded;
	int incomplete;
	int interval;
	int min_interval;	
};

//Connected User Variables
//All peer information goes into here
struct TrackerPeerInfo
{
	int isConnected;
	struct sockaddr_in potential_sock;
	int potential_fd;
	
};
struct TrackerPeerInfo potentialPeers_Info[MAXPOTENTIALPEERS];

//Actually connected peers are in here
struct ConnectionInfo 
{
	uint8_t peer_id[20];
	struct sockaddr_in connection_sock;
	int connection_fd;
	int am_choking;
	int am_interested;
	int peer_choking;
	int peer_interested;
	int peer_bitfield[BitFieldLength];
	int uploadSpeed;
	int downloadSpeed;
};
struct ConnectionInfo connectedClients_Info[MAXCONNECTEDCLIENTS]; 

struct ThreadArguments 
{
	int conIndex;
	int potIndex;
	int someFD;
	struct sockaddr_in someSock;
};
struct ThreadArguments arguments[MAXCONNECTEDCLIENTS];

struct sockaddr_in * ResolveHostNameToIPAddress(char * Address)
{
	/*
	 Created with help from
	 http://beej.us/guide/bgnet/output/html/multipage/getaddrinfoman.html
	 and
	 http://en.wikipedia.org/wiki/Getaddrinfo
	 and
	 http://beej.us/guide/bgnet/examples/showip.c
	 */
	
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];
	
	
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;
	
    if ((status = getaddrinfo(Address, NULL, &hints, &res)) != 0) {
		HostNametoIPAddressStatus = status;
        //printf("getaddrinfo: %s\n", gai_strerror(status));
        return NULL;
    }
	
	
    for(p = res;p != NULL; p = p->ai_next) {
        void *addr;
        char *ipver;
		
        // get the pointer to the address itself,
        // different fields in IPv4 and IPv6:
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
			return ipv4;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        }
		else {
			continue;
		}
		
		
        // convert the IP to a string and print it:
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        printf("  %s: %s\n", ipver, ipstr);
    }
	
    freeaddrinfo(res); // free the linked list
	
    return NULL;
}

struct sockaddr_in GetIPAddress()
{
	//http://ubmnc.wordpress.com/2010/09/22/on-getting-the-ip-name-of-a-machine-for-chatty/
	int GoogleListeningFD;
	struct sockaddr_in GoogleSockAddr, LocalIPSockAddr;
	
	if ((GoogleListeningFD = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		sys_error("Could not open UDP socket");
	}
	
	memset((void *) &GoogleSockAddr, 0, sizeof(GoogleSockAddr));
	GoogleSockAddr.sin_family      = AF_INET;
	//http://beej.us/guide/bgnet/output/html/multipage/inet_ntopman.html
	inet_pton(AF_INET, "8.8.8.8", &(GoogleSockAddr.sin_addr));
	GoogleSockAddr.sin_port        = htons(53);
	
	
	if (bind(GoogleListeningFD, (struct sockaddr *) &GoogleSockAddr, sizeof(GoogleSockAddr)) < 0) {
		//sys_error("bind()");
	}
	
	if ( connect(GoogleListeningFD, (struct sockaddr *) &GoogleSockAddr, sizeof(GoogleSockAddr)) < 0 ) {
		sys_error("connect()");
	}
	
	unsigned int len = sizeof(LocalIPSockAddr);
	if (getsockname(GoogleListeningFD, (struct sockaddr *) &LocalIPSockAddr, &len) < 0)
	{
		sys_error("Could not open a connection to 8.8.8.8");
	}
	
	close(GoogleListeningFD);
	
	return LocalIPSockAddr;
	
}

uint8_t * Hash(char * seed, int length)
{
	int         err;
	SHA1Context sha;
	static uint8_t     message_digest[20] = {'\0'}; /* 160-bit SHA1 hash value */
	
	err = SHA1Reset(&sha);
	if (err) app_error("SHA1Reset error %d\n", err);
	
	/* 'seed' is the string we want to compute the hash of */
	err = SHA1Input(&sha, (const unsigned char *) seed, length);
	
	if (err) app_error("SHA1Input Error %d.\n", err );
	
	err = SHA1Result(&sha, message_digest);
	if (err) {
		app_error("SHA1Result Error %d, could not compute message digest.\n", err);
	}
	return	message_digest;
}

char * Label;
void extractMetaData(be_node *node)
{
	size_t i;
	switch (node->type) {
		case BE_STR:
			if (strcmp("announce", Label) == 0) {
				strcpy(metaInformation.announce, node->val.s);
			}
			else if(strcmp("name", Label) == 0) {
				strcpy(metaInformation.info_name, node->val.s);	
			}
			//printf("str = %s (len = %lli)\n", node->val.s, be_str_len(node));
			break;
			
		case BE_INT:
			if (strcmp("creation date", Label) == 0) {
				metaInformation.CreationDate = node->val.i;
			}
			else if	(strcmp("length", Label) == 0) {
				metaInformation.file_info_length = node->val.i;
			}
			else if	(strcmp("piece length", Label) == 0) {
				metaInformation.info_piece_length = node->val.i;
			}
			//printf("int = %lli\n", node->val.i);
			
			break;
			
		case BE_LIST:
			for (i = 0; node->val.l[i]; ++i)
				extractMetaData(node->val.l[i]);
			break;
			
		case BE_DICT:
			
			for (i = 0; node->val.d[i].val; ++i) {
				//printf("%s => ", node->val.d[i].key);
				Label = node->val.d[i].key;
				extractMetaData(node->val.d[i].val);
			}
			break;
	}
}
char * trackerLabel;
void extractTrackerInfo(be_node *node)
{
	size_t i;
	
	switch (node->type) {
		case BE_STR:
			
			break;
			
		case BE_INT:
			if (strcmp("complete", trackerLabel) == 0) {
				trackerInfo.complete = node->val.i;
			}
			else if	(strcmp("downloaded", trackerLabel) == 0) {
				trackerInfo.downloaded = node->val.i;
			}
			else if	(strcmp("incomplete", trackerLabel) == 0) {
				trackerInfo.incomplete = node->val.i;
			}
			else if	(strcmp("interval", trackerLabel) == 0) {
				trackerInfo.interval = node->val.i;
			}
			else if	(strcmp("min interval", trackerLabel) == 0) {
				trackerInfo.min_interval = node->val.i;
			}
			//printf("int = %lli\n", node->val.i);
			
			break;
			
		case BE_LIST:
			for (i = 0; node->val.l[i]; ++i)
				extractTrackerInfo(node->val.l[i]);
			break;
			
		case BE_DICT:
			
			for (i = 0; node->val.d[i].val; ++i) {
				//printf("%s => ", node->val.d[i].key);
				trackerLabel = node->val.d[i].key;
				extractTrackerInfo(node->val.d[i].val);
			}
			break;
	}
}

void BendcodeFile(const char * inputPath)
{
	//http://www.go4expert.com/forums/showthread.php?t=2977
	//http://en.wikipedia.org/wiki/C_file_input/output
	
	//Extract metainfo File name
	char *lastslash = strrchr(inputPath, '/');
	if (lastslash == NULL) {
		strcpy(metaInformation.info_meta_file_name,inputPath);
	}
	else {
		lastslash++;
		strcpy(metaInformation.info_meta_file_name,lastslash);
	}
	
	//Read in the file
	FILE *fp;
	if (!(fp = fopen(inputPath, "r"))) {
		sys_error("The torrent file you supplied was not found");
	}
		
	int c = 0, index = 0, modifyStart = 0;
	int EndOfFile = 0;
	while ((c = fgetc(fp)) != EOF)
	{
		torrentFile[index] = (char)c;
		torrentFileInt[index] = c;
		index++;
	}
	fclose(fp);	
	EndOfFile = index;
	//Extract the Hash (its after pieces)
	char * pch = strstr(torrentFile,"6:pieces") + 8;
	index = 0;
	//Get the Hashes Length
	char val[10] = {'\0'};
	while (pch[index] != ':') {
		val[index] = pch[index];
		index++;
	}
        if (EndOfFile > TORRENTLENGTH)
        {
            printf("The torrent file can not be held in the internal torrent file structure.  Please chose another torrent file to seed.\n");
            exit(0);
        }
	//pch = pch + 4;
	//Fix with file verification; if the amount of pieces is greater than 5 this would not work.
	//Index holds the amount of numbers read and the +2 includes the ':' and the pointer is at the current hash value
	pch = pch + (index + 2);
	int Amount = atoi(val);
	metaInformation.bigHashLength = Amount;
	
	int IntStart = (int)(pch-torrentFile - 1);
	for (int y = 0; y < Amount; y++) 
	{
		metaInformation.bigHash[y] = torrentFileInt[IntStart + y];
	}
	
	//Extract the Hash
	//strncpy(metaInformation.bigHash, pch, Amount);
	pch = pch - index - 1;
	
	//Yank out the Hash portion because the bendcoder will not process the hash
	char modifiedTorrent[TORRENTLENGTH] = {'\0'};
	strcpy(modifiedTorrent,torrentFile);
	modifyStart = (int)(pch-torrentFile);
	modifiedTorrent[modifyStart] = 'e';
	modifiedTorrent[modifyStart+1] = 'e';
	modifyStart += 2;
	
	//Clear the rest of the char array
	for (modifyStart; modifyStart < TORRENTLENGTH; modifyStart++)
	{
		modifiedTorrent[modifyStart] = '\0';
	}
	//printf("%s\n",modifiedTorrent);
	
	//Get the data from the torrent
	be_node *n = be_decode(modifiedTorrent);
	if (n)
	{
		//be_dump(n);
		extractMetaData(n);
		be_free(n);
	} 
	else
	{
		sys_error("The Torrent file is not valid");
	}
	
	//calculate info_hash from the info key value
	char seed[TORRENTLENGTH];
	int seed_length;
	
	char * begin = strstr(torrentFile,"4:info") + 6;
	//char * end = strstr(torrentFile,"ee")+1;
	char * end = torrentFile + EndOfFile -1;
	
	seed_length = end - begin;
	for (int x = 0; x<seed_length; x++) {
		seed[x] =(char)*begin;
		begin++;
	}
	uint8_t * i_hash = Hash(seed, seed_length);
	for (int i = 0; i<MESSAGEDIGESTSIZE; i++) {
		metaInformation.info_hash[i] = i_hash[i];
	}
	
	//Set the BitField
	int amountOfPieces = metaInformation.bigHashLength / MESSAGEDIGESTSIZE;
	for (int x = 0; x < amountOfPieces; x++) {
		pieceField[x] = 0;
	}
}


void MetaInfo()
{
	char IPAddressString[INET_ADDRSTRLEN];
	inet_ntop(AF_INET,&(thisClientAddress.sin_addr),IPAddressString,INET_ADDRSTRLEN);
	
	
	
	printf("my IP/port    : %s/%d\n",IPAddressString,thisPort); 
	printf("my ID         : ");
	for (int x = 0; x < MESSAGEDIGESTSIZE; x++) {
		printf("%02X",myID_hash[x]);		/* The Generated MyID Hash */
	}
	printf("\n");
	printf("metainfo file : %s \n",metaInformation.info_meta_file_name);
	printf("info hash     : ");
	for (int x = 0; x < MESSAGEDIGESTSIZE; x++) {
		printf("%02X",metaInformation.info_hash[x]);
	}
	printf("\n");
	printf("file name     : %s \n",metaInformation.info_name);
	printf("piece length  : %i \n",metaInformation.info_piece_length);
	printf("file size     : %i (%i * [piece length] + %i) \n",metaInformation.file_info_length,numPieces-1,lastPieceSize);
	printf("announce URL  : %s \n",metaInformation.announce);
	printf("pieces' hashes: \n");
	
	int h = 0, row = 0, len = metaInformation.bigHashLength;
	for (h = 0; h < len; h++) 
	{
		if ((h % MESSAGEDIGESTSIZE) == 0)
		{
			if (h != 0) {
				row++;
			}
			printf("\n");
			printf("%02d: ",row);
			fflush(stdout);
		}
		printf("%02X",metaInformation.bigHash[h]);
		fflush(stdout);
	}
}

int ConnectTCP(struct sockaddr_in * connectSocket)
{
	
	int thisFD;
	
	struct sockaddr_in destination;
	memset((void *) &destination, 0, sizeof(destination));
	destination = (struct sockaddr_in) *connectSocket;
	
	if (ntohs(destination.sin_port) <= 0 || ntohs(destination.sin_port) >= 65535) 
	{
		printf("The connect destination port is invalid.  The value must be between 0 and 66535.");
		return -1;
	}
	
	if ((thisFD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("Could not open TCP socket. \n  No connection was made.");
		return -1;
	}
	
	if (connect(thisFD, (struct sockaddr *)&destination, sizeof(destination)) < 0) 
	{
		printf("The connection to the endpoint failed.\n");
		printf("Reason: %s\n",strerror(errno));
		return -1;
	}
	return thisFD;
}

char * URLEncode(uint8_t * encodeSeed, char * encodedString) 
{
	//printf("DEC: %x",encodeSeed[0]);
	memset(encodedString,0,sizeof(encodedString));
	char hexHolder[5];
	int currentByte = 0;
	for (int i = 0; i<20; i++) 
	{
		memset(hexHolder,0,sizeof(hexHolder));
		currentByte = (int)encodeSeed[i];
		if (currentByte >= 48 && currentByte <= 57) 
		{
			memcpy(hexHolder,&encodeSeed[i],1);
			strcat(encodedString,hexHolder);
		}
		else if (currentByte >= 65 && currentByte <= 90)
		{
			memcpy(hexHolder,&encodeSeed[i],1);
			strcat(encodedString,hexHolder);
		}
		else if (currentByte >= 97 && currentByte <= 122)
		{
			memcpy(hexHolder,&encodeSeed[i],1);
			strcat(encodedString,hexHolder);
		}
		else if (currentByte == 45 || currentByte == 46 || currentByte == 95 || currentByte== 126)
		{
			memcpy(hexHolder,&encodeSeed[i],1);
			strcat(encodedString,hexHolder);
		}
		else
		{
			sprintf(hexHolder,"%02X",currentByte);
			strcat(encodedString,"%");
			strcat(encodedString,hexHolder);
		}
		
	}
	return encodedString;
}

int GetTracker(int request, char * recv_line, char * tracker_status)
{
	char trackerIP[INET6_ADDRSTRLEN] = {'\0'};
	char port[10] = {'\0'};
	char trackerGetMsg[FiveTwelve] = {'\0'};
	char formattedMessage[FiveTwelve]= {'\0'};
	char tcp_port[6];
	char fileSize[FiveTwelve] = {'\0'};
	char encodedHash[FiveTwelve] = {'\0'};
	char encodedID[FiveTwelve] = {'\0'};
	uint16_t trackerPortShort = 0;
	uint32_t trackerIPLong = 0;
	int bytes_written;
	int bytes_read;
	int anFD = 0;
	
	// Extract the IP/port from metafile
	char * pch = strstr(metaInformation.announce,"//") + 2;
	int index = 0, jdex = 0;
	while (pch[index] != ':') 
	{
		trackerIP[index] = pch[index];
		index++;
	}
	index++;
	while (pch[index] != '/') 
	{
		port[jdex] = pch[index];
		jdex++;
		index++;
	}
	trackerPort = atoi(port);
	trackerAddress.sin_port = htons(atoi(port));
	inet_pton(AF_INET, trackerIP, &(trackerAddress.sin_addr));
	
	// Connect to tracker and assemble GET message
	trackerFD = ConnectTCP(&trackerAddress);
	strcpy(trackerGetMsg, "GET /announce?info_hash=");
	URLEncode(metaInformation.info_hash,encodedHash);
	strcat(trackerGetMsg,encodedHash);
	strcat(trackerGetMsg,"&peer_id=");
	URLEncode(myID_hash,encodedID);
	strcat(trackerGetMsg,encodedID);
	sprintf(tcp_port,"%d",thisPort);
	strcat(trackerGetMsg,"&port=");
	strcat(trackerGetMsg,tcp_port);
	strcat(trackerGetMsg,"&uploaded=0");
	strcat(trackerGetMsg,"&downloaded=0");
	
	// Are we a seeder or a leecher?
	if(isSeeder == 1)
	{
		//printf("%s is present\n",metaInformation.info_name);
		strcat(trackerGetMsg,"&left=0");
	}
	// If file is not present we are a leecher
	else
	{
		sprintf(fileSize,"%d",metaInformation.file_info_length);
		strcat(trackerGetMsg,"&left=");
		strcat(trackerGetMsg,fileSize);
	}
	
	strcat(trackerGetMsg,"&compact=1");
	
	// Determine which type of event this GET should contain
	if (request == 0) 
	{
		strcat(trackerGetMsg,"&event=started");
	}
	else if (request == 1)
	{
		strcat(trackerGetMsg,"&event=stopped");
	}
	else if (request == 2)
	{
		strcat(trackerGetMsg,"&event=completed");
	}
	strcat(trackerGetMsg," ");
	strcat(trackerGetMsg,"HTTP/1.1\r\n\r\n");
	bytes_written = writen(trackerFD, (const void*) trackerGetMsg, strlen(trackerGetMsg));
	if (bytes_written < 0) 
	{
		printf("Error writing to tracker's socket.");
	}
	
	// Checking the tracker's HTTP status code
	rio_t rio;
	rio_readinitb(&rio, trackerFD);
	bytes_read = rio_readlineb(&rio, recv_line, MAXLINE);
	
	if (bytes_read < 0) 
	{
		report_error("There was an error reading the data from the TCP socket");
		return bytes_read;
	} 
	else if (bytes_read == 0) 
	{
		printf("Connection closed prematurely by tracker.\n");
		close(trackerFD);
		return bytes_read;
	}
	
	int y = 0;
	for (y = 0; y < bytes_read; y++)
	{
		formattedMessage[y] = recv_line[y];
	}
	strcpy(tracker_status,formattedMessage);
	
	// Ignoring intermediary lines
	rio_readlineb(&rio, recv_line, MAXLINE);
	rio_readlineb(&rio, recv_line, MAXLINE);
	rio_readlineb(&rio, recv_line, MAXLINE);
	
	// Reading tracker's bencoded dictionary response
	bytes_read = rio_readlineb(&rio, recv_line, MAXLINE);
	if (bytes_read < 0) 
	{
		report_error("There was an error reading the data from the TCP file descriptor");
		return bytes_read;
	} 
	else if (bytes_read == 0) 
	{
		printf("Connection closed by peer.\n");
		close(trackerFD);
		//connectedClients_fds[connect_id] = -1;
		//app_error("Connection %d quit\n", connect_id);
		return bytes_read;
	}
	close(trackerFD);
	return bytes_read;
}

void DecodeTrackerResponse(char * response)
{
	char segmentedResponse[FiveTwelve] = {'\0'};
	int peerInfo[FiveTwelve] = {'\0'};
	char * pch = strstr(response,"5:peers") + 7;
	int index = 0, numPeerBytes = 0, modifyStart = 0;
	
	// Extract the number of peers we have (1 peer = 6 bytes)
	char val[10] = {'\0'};
	while (pch[index] != ':') 
	{
		val[index] = pch[index];
		index++;
	}
	index++;
	numPeerBytes = atoi(val);
	
	// Extract peers' IPs/Ports
	struct sockaddr_in peerAddr;
	char ipaddress[INET_ADDRSTRLEN] = {'\0'};
	uint32_t peerIP = 0;
	uint16_t peerPort = 0;

	// Set the potentialPeers_Info structure according to the tracker's response.
	for (int i = 0; i < MAXPOTENTIALPEERS ; i++)
	{
		memcpy(&peerIP,&pch[index+(6*i)],4);
		memcpy(&peerPort,&pch[index+(6*i)+4],2);
		memset((void *) &peerAddr, 0, sizeof(peerAddr));
		peerAddr.sin_addr.s_addr = peerIP;
		peerAddr.sin_family      = AF_INET;
		peerAddr.sin_port        = peerPort;
		inet_ntop(AF_INET,&peerAddr.sin_addr,ipaddress,INET_ADDRSTRLEN);
		if (i <= ((numPeerBytes/6)-1))  
		{
			potentialPeers_Info[i].potential_sock = peerAddr;
			potentialPeers_Info[i].potential_fd = 0;
			potentialPeers_Info[i].isConnected = 0;
		}
		else
		{
			memset((void *) &potentialPeers_Info[i].potential_sock, 0, sizeof(potentialPeers_Info[i].potential_sock));
			potentialPeers_Info[i].potential_fd = -1;
			potentialPeers_Info[i].isConnected = -1;
		}
	}
	
	strcpy(segmentedResponse,response);
	pch = pch - 7;
	modifyStart = (int)(pch-response);
	segmentedResponse[modifyStart] = 'e';
	modifyStart++;
	
	// Clear the peer ip/port info
	for (modifyStart; modifyStart < FiveTwelve; modifyStart++)
	{
		segmentedResponse[modifyStart] = '\0';
	}
	
	// Decode the tracker's bencoded dictionary response
	be_node *n = be_decode(segmentedResponse);
	if (n)
	{
		//be_dump(n);
		extractTrackerInfo(n);
		be_free(n);
	} 
	else
	{
		sys_error("The tracker's response is not valid");
	}
	return;
}

void ShowTrackerInfo()
{
	char ipaddress[INET_ADDRSTRLEN] = {'\0'};
	char firstLine[FiveTwelve] = {'\0'};
	char secondLine[FiveTwelve] = {'\0'};
	char comp[15],incomp[15],dl[15],interval[15],minint[15],peerPort[15];
	sprintf(comp,"%d",trackerInfo.complete);
	sprintf(incomp,"%d",trackerInfo.incomplete);
	sprintf(dl,"%d",trackerInfo.downloaded);
	sprintf(interval,"%d",trackerInfo.interval);
	sprintf(minint,"%d",trackerInfo.min_interval);
	strcpy(firstLine," complete");
	strcpy(secondLine," ");
	strcat(secondLine,comp);	
	for (int i = 0; i < strlen("complete")-strlen(comp); i++)
	{
		strcat(secondLine," ");
	}
	strcat(firstLine," | ");
	strcat(firstLine,"downloaded");
	strcat(secondLine," | ");
	strcat(secondLine,dl);
	for (int i = 0; i < strlen("downloaded")-strlen(dl); i++)
	{
		strcat(secondLine," ");
	}
	strcat(firstLine," | ");
	strcat(firstLine,"incomplete");
	strcat(secondLine," | ");
	strcat(secondLine,incomp);
	for (int i = 0; i < strlen("incomplete")-strlen(incomp); i++)
	{
		strcat(secondLine," ");
	}
	strcat(firstLine," | ");
	strcat(firstLine,"interval");
	strcat(secondLine," | ");
	strcat(secondLine,interval);
	for (int i = 0; i < strlen("interval")-strlen(interval); i++)
	{
		strcat(secondLine," ");
	}
	strcat(firstLine," | ");
	strcat(firstLine,"min interval");
	strcat(secondLine," | ");
	strcat(secondLine,minint);
	for (int i = 0; i < strlen("min interval")-strlen(minint); i++)
	{
		strcat(secondLine," ");
	}
	strcat(firstLine," |");
	strcat(secondLine," |");
	printf("%s\n",firstLine);
	for (int i = 0; i < strlen(firstLine); i++)
	{
		printf("-");
	}
	printf("\n");
	printf("%s\n",secondLine);
	for (int i = 0; i < strlen(firstLine); i++)
	{
		printf("-");
	}
	printf("\n");
	
	//bzero(firstLine, FiveTwelve);
	memset(firstLine,0,FiveTwelve);
	//bzero(secondLine, FiveTwelve);
	memset(secondLine,0,FiveTwelve);
	printf("++++ Peer List (self included):\n");
	strcpy(firstLine," IP");
	for (int i = 0; i < INET_ADDRSTRLEN - strlen("IP"); i++)
	{
		strcat(firstLine," ");
	}
	strcat(firstLine,"| ");
	strcat(firstLine,"Port");
	for (int i = 0; i < 7; i++)
	{
		strcat(firstLine," ");
	}
	printf("%s\n",firstLine);
	for (int i = 0; i < strlen(firstLine); i++)
	{
		printf("-");
	}
	printf("\n");
	
	// Print out our peer list
	for (int i = 0; i < MAXPOTENTIALPEERS; i++)
	{
		if (potentialPeers_Info[i].isConnected >= 0)
		{
			strcpy(secondLine," ");
			inet_ntop(AF_INET,&potentialPeers_Info[i].potential_sock.sin_addr,ipaddress,INET_ADDRSTRLEN);
			strcat(secondLine, ipaddress);
			for (int i = 0; i < INET_ADDRSTRLEN-strlen(ipaddress); i++)
			{
				strcat(secondLine," ");
			}
			strcat(secondLine,"| ");
			sprintf(peerPort,"%d", ntohs(potentialPeers_Info[i].potential_sock.sin_port));
			strcat(secondLine,peerPort);
			for (int i = 0; i < 7-strlen(peerPort); i++)
			{
				strcat(secondLine," ");
			}
			printf("%s\n",secondLine);
			//bzero(secondLine,FiveTwelve);
			memset(secondLine,0,FiveTwelve);
		}
		
	}
	return;
}

void Announce()
{
	char recv_line[FiveTwelve] = {'\0'};
	char trackerResponse[FiveTwelve] = {'\0'};
	int bytes_read = 0;
	bytes_read = GetTracker(STARTTRACKER,recv_line,trackerResponse);
	
	// Check the tracker's response to see if it is valid
	printf("++++ Tracker responded: %s",trackerResponse);
	if (trackerResponse[9] == '2') 
	{
		DecodeTrackerResponse(recv_line);
		ShowTrackerInfo();
		printf("\n  ... Tracker closed connection\n");
		close(trackerFD);
	}
	else
	{
		printf("There was an error connecting to tracker.\n");
	}
	return;
}

void ShowConnections()
{
	char ipaddress[INET_ADDRSTRLEN] = {'\0'};
	char firstLine[SixteenK] = {'\0'};
	char secondLine[SixteenK] = {'\0'};
	char id[10],status[16],bit[BitFieldLength] ,dls[16],uls[15],ac[5],ai[5],pc[5],pi[5], portchar[6];
	strcpy(firstLine," ID");
	strcat(firstLine," | ");
	strcat(firstLine,"IP address");
	for (int i = 0; i < 27 - strlen("IP address"); i++)
	{
		strcat(firstLine," ");
	}
	strcat(firstLine," | ");
	strcat(firstLine,"Status");

	strcat(firstLine," | ");
	strcat(firstLine,"Bitfield");
	for (int i = 0; i < (int)bufferToByteSize*8 - strlen("Bitfield"); i++)
	{
		strcat(firstLine," ");
	}

	strcat(firstLine," | ");
	strcat(firstLine,"Down/s");
	for (int i = 0; i < DLULPRECISION - strlen("Down/s"); i++)
	{
		strcat(firstLine," ");
	}
	
	strcat(firstLine," | ");
	strcat(firstLine,"Up/s");
	for (int i = 0; i < DLULPRECISION - strlen("Up/s"); i++)
	{
		strcat(firstLine," ");
	}
	
	strcat(firstLine," |");
	printf("%s\n",firstLine);
	
	for (int i = 0; i < strlen(firstLine); i++)
	{
		printf("-");
	}
	printf("\n");
	
	//bzero(firstLine, FiveTwelve);
	memset(firstLine,0,FiveTwelve);
	//bzero(secondLine, FiveTwelve);
	
	// Print out our peer list
	for (int i = 0; i < MAXCONNECTEDCLIENTS; i++)
	{
		if (connectedClients_Info[i].connection_fd != -1)
		{
			strcpy(secondLine," ");
			sprintf(id,"%d",i);
			strcat(secondLine, id);
			for (int x = 0; x < strlen("ID") - strlen(id); x++)
			{
				strcat(secondLine," ");
			}
			strcat(secondLine," | ");
			inet_ntop(AF_INET,&connectedClients_Info[i].connection_sock.sin_addr,ipaddress,INET_ADDRSTRLEN);
			strcat(secondLine, ipaddress);
                        strcat(secondLine, "/");
                        int port = ntohs(connectedClients_Info[i].connection_sock.sin_port);
                        sprintf(portchar,"%d",port);
                        strcat(secondLine,portchar);
			for (int x = 0; x < 26-(strlen(ipaddress) + strlen(portchar)) ; x++)
			{
				strcat(secondLine," ");

			}
			strcat(secondLine," | ");
			sprintf(ac,"%d",connectedClients_Info[i].am_choking);
			sprintf(ai,"%d",connectedClients_Info[i].am_interested);
			sprintf(pc,"%d",connectedClients_Info[i].peer_choking);
			sprintf(pi,"%d",connectedClients_Info[i].peer_interested);
			strcpy(status,ac);
			strcat(status,ai);
			strcat(status,pc);
			strcat(status,pi);
			strcat(secondLine,status);
			for (int x = 0; x < strlen("Status")-strlen(status); x++)
			{
				strcat(secondLine," ");
			}
			for (int x = 0; x < (int)bufferToByteSize*8; x++)
			{
				sprintf(&bit[x],"%d",connectedClients_Info[i].peer_bitfield[x]);
			}
			strcat(secondLine," | ");
			strcat(secondLine,bit);
			strcat(secondLine," | ");
			sprintf(dls,"%d",connectedClients_Info[i].downloadSpeed);
			strcat(secondLine,dls);
			for (int x = 0; x < DLULPRECISION - strlen(dls); x++)
			{
				strcat(secondLine," ");
			}
			strcat(secondLine," | ");
			sprintf(uls,"%d",connectedClients_Info[i].uploadSpeed);
			strcat(secondLine,uls);
			for (int x = 0; x < DLULPRECISION - strlen(uls); x++)
			{
				strcat(secondLine," ");
			}
			strcat(secondLine," |");
			printf("%s\n",secondLine);
			//bzero(secondLine,SixteenK);
			memset(secondLine,0,SixteenK);
		}
		
	}
	return;
}

void ShowStatus()
{
	char firstLine[SixteenK] = {'\0'};
	char secondLine[SixteenK] = {'\0'};
	char dl[16] = {'\0'},ul[16] = {'\0'},left[16] = {'\0'},bit[BitFieldLength] = {'\0'}; 
	sprintf(dl,"%d",totalDownload);
	sprintf(ul,"%d",totalUpload);
	int amountOfFile = 0;
	for (int x = 0; x < numPieces; x++)
	{
		if (thisBitField[x] == 1 && x != (numPieces-1))
		{
			amountOfFile = amountOfFile + metaInformation.info_piece_length;
		}
		else if (thisBitField[x] == 1 && x == (numPieces-1))
		{
			amountOfFile = amountOfFile + lastPieceSize;
		}
	}
	totalLeft = metaInformation.file_info_length - amountOfFile;
	sprintf(left,"%d",totalLeft);
	strcat(firstLine," Downloaded");
	if (strlen("Downloaded") < strlen(dl))
	{
		for (int i = 0; i < strlen(dl) - strlen("Downloaded"); i++)
		{
			strcat(firstLine," ");
		}
	}
	strcat(firstLine," | ");
	strcat(firstLine,"Uploaded");
	if (strlen("Uploaded") < strlen(ul))
	{
		for (int i = 0; i < strlen(ul) - strlen("Uploaded"); i++)
		{
			strcat(firstLine," ");
		}
	}
	strcat(firstLine," | ");
	strcat(firstLine,"Left");
	if (strlen("Left") < strlen(left))
	{
		for (int i = 0; i < strlen(left) - strlen("Left"); i++)
		{
			strcat(firstLine," ");
		}
	}
	strcat(firstLine," | ");
	strcat(firstLine,"My bit field");
	if (bufferToByteSize > strlen("My bit field"))
	{
		for (int i = 0; i < bufferToByteSize - strlen("My bit field"); i++)
		{
			strcat(firstLine," ");
		}
	}
	printf("%s\n",firstLine);
	strcpy(secondLine," ");
	strcat(secondLine,dl);
	if (strlen(dl) < strlen("Downloaded"))
	{
		for (int i = 0; i < strlen("Downloaded") - strlen(dl); i++)
		{
			strcat(secondLine," ");
		}
	}
	strcat(secondLine," | ");
	strcat(secondLine,ul);
	if (strlen(ul) < strlen("Uploaded"))
	{
		for (int i = 0; i < strlen("Uploaded") - strlen(ul); i++)
		{
			strcat(secondLine," ");
		}
	}
	strcat(secondLine," | ");
	strcat(secondLine,left);
	if (strlen(left) < strlen("Left"))
	{
		for (int i = 0; i < strlen("Left") - strlen(left); i++)
		{
			strcat(secondLine," ");
		}
	}
	strcat(secondLine," | ");
	for (int x = 0; x < (int)bufferToByteSize*8; x++)
	{
		sprintf(&bit[x],"%d",thisBitField[x]);
	}
	strcat(secondLine,bit);
	for (int i = 0; i < max(strlen(firstLine),strlen(secondLine)); i++)
	{
		printf("-");
	}
	printf("\n");
	printf("%s\n",secondLine);
	//bzero(firstLine,SixteenK);
	memset(firstLine,0,SixteenK);
	//bzero(secondLine,SixteenK);
	memset(secondLine,0,SixteenK);
	
}

void Quit()
{
	char recv_line[FiveTwelve] = {'\0'};
	char trackerResponse[FiveTwelve] = {'\0'};
	GetTracker(STOPTRACKER,recv_line,trackerResponse);
	exit(0);
}

void ProcessConsoleInput(char consoleInput[TORRENTLENGTH])
{
	if (strcmp(consoleInput, "metainfo") == 0) 
	{
		MetaInfo();
	}
	else if (strcmp(consoleInput, "announce") == 0)
	{
		Announce();
	}
	else if (strcmp(consoleInput, "trackerinfo") == 0)
	{
		ShowTrackerInfo();
	}
	else if (strcmp(consoleInput, "show") == 0)
	{
		ShowConnections();
	}
	else if (strcmp(consoleInput, "status") == 0)
	{
		ShowStatus();
	}
	else if (strcmp(consoleInput, "quit") == 0)
	{
		Quit();
	}
	else {
		printf("Invalid Command.\n");
	}
	
}

//Calculates the my_ID from the time and a random number
void GenerateID()
{
	char ID[128] = {'\0'};
	time_t seconds = time(NULL);
	srand(time(NULL));
	sprintf(ID,"%d",rand());
	strcat(ID,ctime(&seconds));
	uint8_t * i_hash = Hash(ID, 128);
	for (int i = 0; i<20; i++) {
		myID_hash[i] = i_hash[i];
	}
}

//The thread that will handle one connection to and from a peer
//It will handle reading and writing data to and from a peer


/*
	This function will take in a uint8 bitfield and return (in an argument) an integer representation of the bitfield
	The function will return -1 if too many bits are set versus the amount of pieces in the file
	The function will return 0 if successful.
 */
int ConvertBitFieldtoIntegerArray(uint8_t receivedBitField[BitFieldLengthuint8], int convertedBitField[BitFieldLength])
{
	int amountOfPieces = metaInformation.bigHashLength / MESSAGEDIGESTSIZE;
//	int amountOfPieces = 14;
	memset(convertedBitField,0,BitFieldLength);
	for (int x = 0; x < BitFieldLengthuint8; x++)
	{
		int offset = 8 * x;
		if (offset >= amountOfPieces) {
			return 0;
		}
		
		if ((receivedBitField[x] & 128) == 128) {	//1st bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 64) == 64) {		//2nd bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}			
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 32) == 32) {		//3rd bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 16) == 16) {		//4th bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}	
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 8) == 8) {		//5th bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 4) == 4) {		//6th bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 2) == 2) {		//7th bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
		offset++;
		
		if ((receivedBitField[x] & 1) == 1) {		//8th bit
			if (offset>=amountOfPieces) {
				return -1;	//A bit was set past the end the of bit field
			}
			convertedBitField[offset] = 1;
		}
	}
	return 0;
}

/*
	This function will take in an integer BitField are return (in an argument) a representation in uint8.
	The function will return how far one needs to read until in the convertedBitField
	For example, if there are 10 pieces, then 1 is returned. (index 0 and 1)
				 if there are 34 pieces, then 4 is returned. (index 0,1,2,3,4)
 */
int ConvertIntegerArraytoBitField(int BitField[BitFieldLength], uint8_t convertedBitField[BitFieldLengthuint8])
{
	memset(convertedBitField,0,BitFieldLengthuint8);
	int amountOfPieces = metaInformation.bigHashLength / MESSAGEDIGESTSIZE;
//	int amountOfPieces = 10;
	
	for (int x = 0; x < BitFieldLengthuint8; x++)
	{
		uint8_t value = 0;
		int offset = x * 8;
		if (BitField[offset++] == 1) {	//1st bit
			value = value | 128;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//2nd bit
			value = value | 64;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//3rd bit
			value = value | 32;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//4th bit
			value = value | 16;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//5th bit
			value = value | 8;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//6th bit
			value = value | 4;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//7th bit
			value = value | 2;
		}
		if (offset >= amountOfPieces) { //If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		if (BitField[offset++] == 1) {	//8th bit
			value = value | 1;
		}
		if (offset >= amountOfPieces) {	//If there are no more pieces, return the bit field
			convertedBitField[x] = value;
			return x;
		}
		
		convertedBitField[x] = value;
	}
	return 9;
}

//If all of the bits in the field are one then we are a seeder
void VerifySeederStatusVsBitField()
{
	int x = 0;
	while (1)
	{
		if (pieceField[x] == -1) {
			break;
		}
		if (pieceField[x] == 0) {
			return;
		}
		x++;
	}
	isSeeder = 1;
}

void VerifyBitField(int ShouldQuitIfNotValid)
{
	FILE *fp = fopen(metaInformation.info_name, "r"); // Open file for reading
	int TotalBlocks = metaInformation.bigHashLength / MESSAGEDIGESTSIZE;
	for (int x = 0; x<TotalBlocks; x++) 
	{
		char buffer[TwoFiftySixK] = {'\0'};
		int AmountRead = 0, z = 0, Verified = 1;
		uint8_t * message_digest = {'\0'};
		
		AmountRead = fread(buffer, 1 /*Byte*/, metaInformation.info_piece_length, fp);
		message_digest = Hash(buffer, AmountRead);
		for (int y = 0; y < MESSAGEDIGESTSIZE; y++) {
			int offset = (x * MESSAGEDIGESTSIZE) + y;
			if (!(message_digest[y] == metaInformation.bigHash[offset]))
			{
				if (ShouldQuitIfNotValid == 1) {
					app_error("When the file was being verified there was an error inside.\n Please delete the file and restart the application.");
				}
				Verified = 0; break;	
			}
			z++;
		}
		if (Verified == 1)
		{ //printf("Verified: %d\n",x);
			pieceField[x] = 1;}
	}	
	VerifySeederStatusVsBitField();
	fclose(fp);
}

//The Main function that will write to the file at the top of the page
//This must be the only function that handles writing to the file
//Note that a block is inside the piece  (There are multiple blocks in a piece)
// A piece is made of multiple blocks
int FileWriter(int pieceIndex, int BeginOffset, unsigned char buffer[TwoFiftySixK], int LengthToWrite)
{
	pthread_mutex_lock(&writeMutex);
	
	FILE *fp = fopen(metaInformation.info_name, "r+"); // Open file for reading and writing ANYWHERE in the file
														//Append (a) will only let you append to the end of the file and you can't rewind to the beginning
	
	//The Location to seek to is calculated below
	int FileSeekLocation = (pieceIndex * metaInformation.info_piece_length) + BeginOffset;
	
	
	//The part that you seeked to is past the end of the file.
	if (FileSeekLocation > metaInformation.file_info_length) {
		pthread_mutex_unlock(&writeMutex);
		return -2;
	}
	//You are writing past the end of the file
	if ((FileSeekLocation + LengthToWrite) > metaInformation.file_info_length) {
		pthread_mutex_unlock(&writeMutex);
		return -3;
	}
	
	fseek(fp, FileSeekLocation, SEEK_SET);	//Seek to point to read from the beginning of the file
	fwrite(buffer, 1 /* byte size */, LengthToWrite, fp);
	fclose(fp);		
	
	VerifyBitField(0);
	pthread_mutex_unlock(&writeMutex);
	return 0;
}

//Arrays are passed by reference by default
//Return the amount of bytes read.  
int FileReader(int pieceIndex, int BlockOffset, int LengthToRead, unsigned char Data[TwoFiftySixK])
{
	pthread_mutex_lock(&readMutex);
	
	FILE *fp = fopen(metaInformation.info_name, "r"); // Open file for reading
	unsigned char buffer[TwoFiftySixK] = {'\0'};
	int FileSeekLocation = (pieceIndex * metaInformation.info_piece_length) + BlockOffset;
	
	//The part that you seeked to is past the end of the file.
	if (FileSeekLocation > metaInformation.file_info_length) {
		pthread_mutex_unlock(&readMutex);
		return -2;
	}
	
	//You are reading past the end of the file
	if ((FileSeekLocation + LengthToRead) > metaInformation.file_info_length) {
		pthread_mutex_unlock(&readMutex);
		return -3;
	}
	
	fseek(fp, FileSeekLocation, SEEK_SET);	//Seek to point to read in file
	int ReadBytes = fread(buffer, 1 /*Byte*/, LengthToRead, fp);	//Read data out
	fclose(fp);	
	for (int x = 0; x < ReadBytes;x++) {
		Data[x] = buffer[x];
	}
	
	pthread_mutex_unlock(&readMutex);
	return ReadBytes;
}

void VerifyFile()
{
	//Check if file is present and is the correct size
	struct stat st;
	int statresponse = stat(metaInformation.info_name,&st);
	
	if(statresponse == 0)
	{
		int size = st.st_size;
		if (size == metaInformation.file_info_length) 
		{
			VerifyBitField(0);
		}
		else 
		{
			printf("The file size is incorrect. Please delete file and relaunch the application.");
			exit(0);	
		}
	}
	else //The File does not exist make a file full of blank characters
	{
		FILE *fp = fopen(metaInformation.info_name, "w+"); // Open file for writing
		for (int x = 0; x < metaInformation.file_info_length; x++) 
		{
			fputc(0, fp);	//Make a blank file with nothing in it.
		}
		fclose(fp);
	}

}

void ChangePeerConnectionStatus(int index, int new_status)
{
	pthread_mutex_lock (&mutexPeerConnected);
	
	potentialPeers_Info[index].isConnected = new_status;
	
	pthread_mutex_unlock (&mutexPeerConnected);
	return;
}

int CheckAvailableConnection(int fd)
{
	int index = -1;
	pthread_mutex_lock (&mutexConnectedClients);
	for (int x = 0; x < MAXCONNECTEDCLIENTS; x++)
	{
		if (connectedClients_Info[x].connection_fd == -1)
		{
			connectedClients_Info[x].connection_fd == fd;
			index = x;
			break;
		}
	}
	pthread_mutex_unlock (&mutexConnectedClients);
	return index;
}

void ResetConnection(int index)
{
	pthread_mutex_lock (&mutexConnectedClients);

	connectedClients_Info[index].connection_fd = -1;

	pthread_mutex_unlock (&mutexConnectedClients);
}


int SendMessage(int fd, int msgID, int msgLen, uint8_t * payload, int payloadToSend)
{
	uint8_t messageID = msgID;
	uint32_t messageLength = msgLen;
	uint8_t sendMsg[TwoFiftySixK*2] = {'\0'};
	int bytes_written;
	messageLength = htonl(messageLength);
	memcpy(sendMsg,&messageLength,4);
	memcpy(&sendMsg[4],&messageID,1);
	if (payloadToSend == 1)
	{
		memcpy(&sendMsg[5],payload,msgLen-1);
	}
	//printf("About to send ID: %d, Length: %d\n",msgID, msgLen+4);

	bytes_written = writen(fd, (const void*) sendMsg, msgLen+4);

	if (bytes_written < 0) 
	{
		//printf("Error writing to socket.");
		return -1;
	}
	else 
	{
		//printf("Wrote: %d bytes\n",bytes_written);
		return 0;
	}
}

void SendGlobalHaveMessage(int pieceIndex) 
{
	uint8_t payloadToSend[10] = {'\0'};
	int fd = -1;
	pieceIndex = htonl(pieceIndex);
	memcpy(payloadToSend,&pieceIndex,4);
	for (int x = 0; x < MAXCONNECTEDCLIENTS; x++)
	{
		fd = connectedClients_Info[x].connection_fd;
		if (fd != -1)
		{
			SendMessage(fd, 4, 5, payloadToSend,1);
		}
	}
//	printf("Sending have message for piece: %d\n",ntohl(pieceIndex));
	return;
}

// Reads from sockets and closes threads on errors or if peer closes.
void ReadMessage(int fd, int pIndex, int cIndex, uint8_t * buffer, int num_bytes)
{
	int bytes_read = readn(fd, (void *) buffer, num_bytes);
	if (bytes_read == 0)
	{
		printf("Client closed socket\n");
		close(fd);
		ResetConnection(cIndex);
		if (pIndex != -1)
		{
			potentialPeers_Info[pIndex].isConnected == 0;
		}
		pthread_exit(0);
	}
	else if (bytes_read < 0) 
	{
		//sys_error("ReadMessage()");
		printf("Error reading from socket\n");
		close(fd);
		ResetConnection(cIndex);
		if (pIndex != -1)
		{
			potentialPeers_Info[pIndex].isConnected == 0;
		}
		pthread_exit(0);
	}
}


void UpdatePeerBitfield(int cIndex, int pieceIndex)
{
	pthread_mutex_lock (&mutexBitfieldUpdate);
	
	connectedClients_Info[cIndex].peer_bitfield[pieceIndex] = 1;
	
	pthread_mutex_unlock (&mutexBitfieldUpdate);
	return;
}

void UpdateDownloaded(int amount)
{
	pthread_mutex_lock (&mutexUpdateDL);
	
	totalDownload = totalDownload + amount;
	
	pthread_mutex_unlock (&mutexUpdateDL);
	return;
}
void UpdateUploaded(int amount)
{
	pthread_mutex_lock (&mutexUpdateUL);
	
	totalUpload = totalUpload + amount;
	
	pthread_mutex_unlock (&mutexUpdateUL);
}

int RequestPiece()
{
	pthread_mutex_lock (&mutexPieceRequest);
		// Requests rarest first
		int pieceTotalArray[BitFieldLength] = {0};
		int rarestPieceArray[BitFieldLength] = {0};
		int minIndex = -1;
		int minValue = MAXCONNECTEDCLIENTS+1;
		int numMin = 0;
		// Perform a bitwise-add for all of our peers' bitfields.
		// This will give us the totals for each piece in the swarm.
		for (int y = 0; y < MAXCONNECTEDCLIENTS; y++)
		{
			if (connectedClients_Info[y].connection_fd != -1)
			{
				for (int x = 0; x < numPieces; x++)
				{
					if (thisBitField[x] == 1)
					{
						pieceTotalArray[x] = MAXCONNECTEDCLIENTS + 1;
					}
					else if (thisBitField[x] == 0 && connectedClients_Info[y].peer_bitfield[x] == 1)
					{
						pieceTotalArray[x]++;
					}
				}
			}
		}
		for (int x = 0; x < numPieces; x++)
		{
			if (pieceTotalArray[x] != 0)
			{				
				if (pieceTotalArray[x] < minValue)
				{
					minValue = pieceTotalArray[x];
					rarestPieceArray[x] = 1;
					if (minIndex != -1) 
					{				
						rarestPieceArray[minIndex] = 0;
					}
					minIndex = x;
					numMin = 1;
					for (int y = 0; y < x; y++)
					{
						rarestPieceArray[y] = 0;
					}
				}
				else if (pieceTotalArray[x] == minValue)
				{
					rarestPieceArray[x] = 1;
					numMin++;
				}
			}
		}
	pthread_mutex_unlock (&mutexPieceRequest);
	// If there were no pieces that we need then
	// we don't need to request.
	if (numMin == 0)
	{
		return -1;
	}
	// Else we return the index of the rarest
	else if (numMin == 1)
	{
		return minIndex;
	}
	// If there are many rare pieces we select one at random
	else
	{
		//printf("NUM MIN: %d\n", numMin);
		int randNum = rand() % numMin;
		//printf("RAND: %d\n", randNum);
		int count = 0;
		for (int i = 0; i < numPieces; i++)
		{
			if (rarestPieceArray[i] == 1 && count == randNum) 
			{
				return i;
			}
			else if (rarestPieceArray[i] == 1)
			{
				count++;
			}
		}
	}
}

void Handshake(int pIndex, int cIndex, int fd)
{
	fd_set monitoring_set;
	int bytes_written = 0, bytes_read = 0;
	char recv_line[70] = {'\0'};
	memset(recv_line,0,sizeof(recv_line));
	int len = sizeof(recv_line);
	FD_ZERO(&monitoring_set);
	FD_SET(fd, &monitoring_set);
	
	// Create the handshaking message
	uint8_t handshakeMsg[68] = {'\0'};
	uint8_t protocolLen = 19;
	char protocolString[19] = {"BitTorrent protocol"};
	
	 memset(handshakeMsg,0,sizeof(handshakeMsg));
	 //memset(protocolString,0,sizeof(protocolString));
 	 memcpy(handshakeMsg,&protocolLen,1);
	int index = 0;
	for (index = 1; index < 20; index++)
	{	handshakeMsg[index] = protocolString[index-1];	}
	for (index = 20; index < 28; index++)
		{	handshakeMsg[index] = 0;}
	for (index = 28; index < 48; index++)
		{handshakeMsg[index] = metaInformation.info_hash[index - 28];}
	for (index = 48; index < 68; index ++)
		{ handshakeMsg[index] =  myID_hash[index-48];}
	 int writeLen = 68;
	
	// Write the handshake message to socket
	bytes_written = writen(fd, (const void*) handshakeMsg, writeLen);
	
	if (bytes_written < 0) 
	{
		printf("Error writing to fd.");
		close(fd);
		ResetConnection(cIndex);
		if (pIndex != -1)
		{
			potentialPeers_Info[pIndex].isConnected == 0;
		}
		pthread_exit(0);
	}
	
	// Wait 2 seconds for response handshake, otherwise close connection
	int MaxDescriptor = fd + 1;
	struct timeval tv;
	tv.tv_sec = 2;
	int ChangedFDs = select(MaxDescriptor, &monitoring_set, NULL, NULL,&tv);
	if (FD_ISSET(fd,&monitoring_set))
	{
		char blah[28] = {'\0'};
		uint8_t standardStuff[28] = {'\0'};
		uint8_t hash[20] = {'\0'};
		// Read through the pstrlen, protocol, and reserved bytes
		ReadMessage(fd, pIndex, cIndex, standardStuff, 28);
		// Parse out the peer's info hash and compare to ours
		// If they differ, close the connection
		ReadMessage(fd, pIndex, cIndex, hash, 20);
		int infoHashSame = 1;
		for (int h = 0; h < 20; h++)
		{
			if (hash[h] != metaInformation.info_hash[h]) 
			{
				infoHashSame = 0;
			}
		}
		
		// The info hashes are the same, now get the peer's id
		// Check to make sure we haven't connected to this peer already
		if (infoHashSame)
		{
			memset(hash,0,sizeof(hash));
			bytes_read = readn(fd, (void *) hash, 20);
			int alreadyConnected = 0;
			for (int h = 0; h < MAXCONNECTEDCLIENTS; h++)
			{
				if (connectedClients_Info[h].connection_fd != -1)
				{
					alreadyConnected = 1;
					for (int x = 0; x < 20; x++)
					{
						if (hash[x] != connectedClients_Info[h].peer_id[x]) 
						{
							alreadyConnected = 0;
							break;
						}
						
					}
				}
				if (alreadyConnected == 1)
				{
					if (pIndex != -1)
					{
						potentialPeers_Info[pIndex].isConnected = 1;
					}
					close(fd);
					pthread_exit(0);
				}
			}
			
			// We don't already have a connection to this peer
			// Let's add the peer to the connectedClients_Info structure
			socklen_t len;
			len = sizeof connectedClients_Info[cIndex].connection_sock;
			connectedClients_Info[cIndex].connection_fd = fd;
			getpeername(fd,(struct sockaddr*) &connectedClients_Info[cIndex].connection_sock, &len);
			for (int x = 0; x < 20; x++)
			{
				connectedClients_Info[cIndex].peer_id[x] = hash[x]; 
			}
			for (int x = 0; x < BitFieldLength; x++)
			{
				connectedClients_Info[cIndex].peer_bitfield[x] = 0;
			}
			connectedClients_Info[cIndex].am_choking = 1;
			connectedClients_Info[cIndex].am_interested = 0;
			connectedClients_Info[cIndex].peer_choking = 1;
			connectedClients_Info[cIndex].peer_interested = 0;
		}
		else
		{
			printf("The info hashes don't match.  Dropping peer.\n");
			close(fd);
			ResetConnection(cIndex);
			pthread_exit(0);
		}
	}
	else
	{
		printf("Handshake not received fast enough...  Dropping peer.\n");
		close(fd);
		ResetConnection(cIndex);
		pthread_exit(0);
	}
	FD_ZERO(&monitoring_set);
	return;
	
}

void *OptimisticUnchoke()
{
	int connected_fds[MAXCONNECTEDCLIENTS];
	int connected_uls[MAXCONNECTEDCLIENTS];
	int connected_dls[MAXCONNECTEDCLIENTS];
	int indexes[MAXCONNECTEDCLIENTS];
	int temp_list[MAXCONNECTEDCLIENTS] = {'0'};
	int ranked_list[MAXCONNECTEDCLIENTS];
	time_t tenSecTimeout = time(NULL);
	time_t thirtySecTimeout = time(NULL);
	int totalConnected = 0;
	while(1)
	{
		// Ranking by downld/upld
		if ((time(NULL)-tenSecTimeout) >= 10)
		{
			//printf("IN 10 SEC\n");
			tenSecTimeout = time(NULL);
			for(int x=0; x<MAXCONNECTEDCLIENTS; x++)
			{
				if (connectedClients_Info[x].connection_fd != -1 && connectedClients_Info[x].peer_interested == 1)
				{
					connected_fds[totalConnected] = connectedClients_Info[x].connection_fd;
				       	indexes[totalConnected] = x;
					connected_dls[totalConnected] = connectedClients_Info[x].downloadSpeed;
					connected_uls[totalConnected] = connectedClients_Info[x].uploadSpeed;
					totalConnected++;
				}
			}
			if (totalConnected <= 3)
			{
				for (int i = 0; i < totalConnected; i++)
				{
					SendMessage(connected_fds[i],1,1,NULL,0);
					connectedClients_Info[indexes[i]].am_choking = 0;
				}
			}
			else
			{
				// If seeder, we rank by download.
				if (isSeeder == 1) 
				{
					int count = 1;
					for (int i = 0; i < totalConnected; i++)
					{
					       for (int j = 0; j < count; j++)
					       {
						       if (connected_dls[i] > temp_list[j])
						       {
							       for (int k = count-1; k > j; k--)
							       {
								       temp_list[k] = temp_list[k-1];
								       ranked_list[k] = ranked_list[k-1];
							       }
							       temp_list[j] = connected_dls[i];
							       ranked_list[j] = connected_fds[i];
							       break;
						       }
					       }	       
					count++;
					}
					for (int i = 0; i < 3; i++)
					{
						SendMessage(ranked_list[i],1,1,NULL,0);
						connectedClients_Info[indexes[i]].am_choking = 0;
					}
					for (int i = 3; i < totalConnected; i++)
					{
						SendMessage(ranked_list[i],0,1,NULL,0);
						connectedClients_Info[indexes[i]].am_choking = 1;
					}	
					
				}
				// If leecher, we rank by upload.
				else if (isSeeder == 0)
				{
						int count = 1;
					for (int i = 0; i < totalConnected; i++)
					{
					       for (int j = 0; j < count; j++)
					       {
						       if (connected_uls[i] > temp_list[j])
						       {
							       for (int k = count-1; k > j; k--)
							       {
								       temp_list[k] = temp_list[k-1];
								       ranked_list[k] = ranked_list[k-1];
							       }
							       temp_list[j] = connected_uls[i];
							       ranked_list[j] = connected_fds[i];
							       break;
						       }
					       }	       
					count++;
					}
					for (int i = 0; i < 3; i++)
					{
						SendMessage(ranked_list[i],1,1,NULL,0);
						connectedClients_Info[indexes[i]].am_choking = 0;

					}
					for (int i = 3; i < totalConnected; i++)
					{
						SendMessage(ranked_list[i],0,1,NULL,0);
						connectedClients_Info[indexes[i]].am_choking = 1;

					}	

				}
			}
			memset(connected_fds,0,sizeof(connected_fds));
			memset(connected_uls,0,sizeof(connected_uls));
			memset(connected_dls,0,sizeof(connected_dls));
			memset(temp_list,0,sizeof(temp_list));
			memset(ranked_list,0,sizeof(ranked_list));
		}
		// Optimistic unchoking
		if ((time(NULL)-thirtySecTimeout) >= 30)
		{
			//printf("IN 30 SEC\n");
			thirtySecTimeout = time(NULL);
			int optimistic_potential[MAXCONNECTEDCLIENTS];
			int indexes[MAXCONNECTEDCLIENTS];
			int number_opt = 0;
			for (int x = 0; x < MAXCONNECTEDCLIENTS; x++)
			{
				if (connectedClients_Info[x].peer_interested == 1 && connectedClients_Info[x].am_choking == 1)
				{
					optimistic_potential[number_opt] = connectedClients_Info[x].connection_fd;
					number_opt++;
				}

			}
			// If only one candidate, then optimistically unchoke them
			if (number_opt > 0) 
			{
				if (number_opt == 1)
				{
					SendMessage(optimistic_potential[0],1,1,NULL,0);
					connectedClients_Info[indexes[0]].am_choking = 0;
				}
			// Else, pick one candidate at random to optimistically unchoke
				else
				{
					int randNum = rand() % (number_opt-1);
					SendMessage(optimistic_potential[randNum],1,1,NULL,0);
					connectedClients_Info[indexes[randNum]].am_choking = 0;
				}
				memset(optimistic_potential,0,sizeof(optimistic_potential));
				memset(indexes,0,sizeof(indexes));
			}	
		}
	}
}


uint8_t payloadToSendSeeder[524288];
uint8_t payloadRecvSeeder[524288];
uint8_t pieceDataSeeder[524288];
uint8_t payloadToSend[524288];
uint8_t payloadRecv[524288];
uint8_t pieceData[524288];

void *PeerThread(void * args)
{
	// Extract info from ThreadArguments structure
	struct ThreadArguments *theseArgs ;
	int fd, cIndex, pIndex;
	struct sockaddr_in addr;
	memset((void *) &addr, 0, sizeof(addr));
	theseArgs = (struct ThreadArguments *) args;
	cIndex = theseArgs->conIndex;
	pIndex = theseArgs->potIndex;
	fd = theseArgs->someFD;
	addr = theseArgs->someSock;
	
	uint8_t myBitField[BitFieldLengthuint8];
	uint8_t peerBitField[BitFieldLengthuint8];
	int peerBitFieldInt[BitFieldLength];
	
	fd_set monitoring_set;
	FD_ZERO(&monitoring_set);
	FD_SET(fd, &monitoring_set);
	int MaxDescriptor = fd + 1;
	struct timeval tv;
	struct timeval seeder_tv;
	struct timeval leecher_tv;
	tv.tv_sec = 2;
	seeder_tv.tv_sec = 10;
	leecher_tv.tv_sec = 5;
	time_t leecherRqstTimer = 0;
	time_t updateRates = time(NULL);
	time_t connectionStartTime = time(NULL);
	int downloaded = 0;
	int uploaded = 0;
	
	// Perform handshake and exchange bitfields
	Handshake(pIndex, cIndex, fd);
	int readToIndex = 0;
	readToIndex = ConvertIntegerArraytoBitField(thisBitField, myBitField);
	
	// Send bitfield message, message ID = 5
	int msgID = 5;
	SendMessage(fd, msgID, 1+(readToIndex+1), myBitField,1);
	int ChangedFDs = select(MaxDescriptor, &monitoring_set, NULL, NULL,&tv);
	if (FD_ISSET(fd,&monitoring_set))
	{
		uint32_t readLen;
		uint8_t readLen8[10] = {'\0'};
		uint8_t msgID;
		
		// Read message length prefix
		ReadMessage(fd, pIndex, cIndex, readLen8, 4);
		memcpy(&readLen,readLen8,4);
		readLen = ntohl(readLen);
		
		// Read message ID
		ReadMessage(fd, pIndex, cIndex, &msgID, 1);
		
		// Read message payload
		ReadMessage(fd, pIndex, cIndex, peerBitField, readLen-1);
		int success = ConvertBitFieldtoIntegerArray(peerBitField, connectedClients_Info[cIndex].peer_bitfield);
		if (success < 0) 
		{
// 			printf("Problem converting peer bitfield\n");
		}
//		printf("Received Peer Bitfield\n");
		sleep(1);
	}
	else {
		 //printf("TIMEOUT!!\n"); 
		 }
	while(1)
	{
		if ((time(NULL) - updateRates) >= 0)
		{
			connectedClients_Info[cIndex].uploadSpeed = (int)round((uploaded/(time(NULL)-connectionStartTime)));
			connectedClients_Info[cIndex].downloadSpeed = (int)round((downloaded/(time(NULL)-connectionStartTime)));
			updateRates = time(NULL);
		}
		// Seeder mode
		if (isSeeder == 1) 
		{
			int pieceIndex = -1, begin = -1, reqLength = -1, bytesWritten = 0;
			uint32_t readLen;
			uint8_t readLen8[10] = {'\0'};
			uint8_t msgID;
			FD_ZERO(&monitoring_set);
			FD_SET(fd, &monitoring_set);
			ChangedFDs = select(MaxDescriptor, &monitoring_set, NULL, NULL,&seeder_tv);
			if (FD_ISSET(fd,&monitoring_set))
			{
				ReadMessage(fd, pIndex, cIndex, readLen8, 4);
				memcpy(&readLen,readLen8,4);
				readLen = ntohl(readLen);
				if (readLen != 0) 
				{
					ReadMessage(fd, pIndex, cIndex, &msgID, 1);
				}
				switch (msgID) {
					case CHOKE:
						connectedClients_Info[cIndex].peer_choking = 1;
						break;
					case UNCHOKE:
						connectedClients_Info[cIndex].peer_choking = 0;
						break;
					case INTERESTED:
						connectedClients_Info[cIndex].peer_interested = 1;
						/*bytesWritten = SendMessage(fd, 1, 1, NULL,0);
						if (bytesWritten < 0)
						{
							printf("Error sending unchoke message\n");
						}
						else
						{
							connectedClients_Info[cIndex].am_choking = 0;
						}*/
						break;
					case NOTINTERESTED:
						connectedClients_Info[cIndex].peer_interested = 0;
						break;
					case HAVE:
						ReadMessage(fd, pIndex, cIndex, payloadRecvSeeder, readLen-1);
						memcpy(&pieceIndex,payloadRecvSeeder,4);
						pieceIndex = ntohl(pieceIndex);
						UpdatePeerBitfield(cIndex, pieceIndex);
						//printf("Received Have Message for piece: %d\n",pieceIndex);
						break;
					case BITFIELD:
						// We will read bitfield messages, but will not process them.
						ReadMessage(fd, pIndex, cIndex, peerBitField, readLen-1);
						break;
					case REQUEST:
						ReadMessage(fd, pIndex, cIndex, payloadRecvSeeder, 12);
						memcpy(&pieceIndex,payloadRecvSeeder,4);
						memcpy(&begin,&payloadRecvSeeder[4],4);
						memcpy(&reqLength,&payloadRecvSeeder[8],4);
						pieceIndex = ntohl(pieceIndex);
						begin = ntohl(begin);
						reqLength = ntohl(reqLength);

						//printf("Piece Requested: %d\n",pieceIndex);
						//printf("Begin Request Position: %d\n",begin);
						//printf("Length of Request: %d\n",reqLength);
						int bytes_read =  FileReader(pieceIndex, begin, reqLength, pieceDataSeeder);
  						if (bytes_read < 0)
  						{
  							printf("Peer attampted to read past the EOF.\n");
  						}
  						else
  						{
							uploaded = uploaded + bytes_read;
							UpdateUploaded(bytes_read);
							//printf("Bytes Read: %d\n",bytes_read);
  							pieceIndex = htonl(pieceIndex);
  							begin = htonl(begin);
  							memcpy(payloadToSendSeeder,&pieceIndex,4);
  							memcpy(&payloadToSendSeeder[4],&begin,4);
  							memcpy(&payloadToSendSeeder[8],pieceDataSeeder,bytes_read);
  							bytesWritten = SendMessage(fd,7, 9+bytes_read, payloadToSendSeeder,1);
							if (bytesWritten < 0)
							{
								printf("Error sending piece message\n");
							}
							else
							{
								//printf("Sending over piece: %d\n",ntohl(pieceIndex));
							}
  						}
 
						break;
					case PIECE:
						// Just read out the piece data, but do nothing 
						// with it since we are a seeder
						ReadMessage(fd, pIndex, cIndex, payloadRecvSeeder, readLen-1);
						break;
					case CANCEL:
						break;
				}
			}
		}
		// Leecher mode
		else if (isSeeder == 0)
		{
			int pieceIndex = -1, begin = -1, reqLength = -1, bytesWritten = 0;
			uint32_t readLen;
			uint8_t readLen8[10] = {'\0'};
			uint8_t msgID;
			
			// If the peer has a piece we need and we are not currently interested
			// we send an interested message.
			if (connectedClients_Info[cIndex].am_interested == 0)
			{
				for (int x = 0; x < numPieces; x++)
				{
					if (thisBitField[x] == 0 && connectedClients_Info[cIndex].peer_bitfield[x] == 1)
					{
						bytesWritten = SendMessage(fd, 2, 1, NULL,0);
						if (bytesWritten < 0)
						{
							printf("Error sending am interested message\n");
						}
						else
						{
							connectedClients_Info[cIndex].am_interested = 1;
							//printf("Sending am interested\n");
						}
						//sleep(1);
						break;
					}
				}
			}
			else 
			{
				if (connectedClients_Info[cIndex].peer_choking == 0 && (leecherRqstTimer == 0 || (time(NULL) - leecherRqstTimer) > 10))
				{
					int pieceIndex = RequestPiece();
					// If peer does not have piece we are not interested
					if (pieceIndex < 0)
					{
						SendMessage(fd, 3, 1, NULL, 0);
					}
					// Send request for piece
					else
					{
						int reqLength;
						// Last piece may not be of the full piece size
						if ((pieceIndex+1) == numPieces)
						{
							reqLength = metaInformation.file_info_length - metaInformation.info_piece_length*pieceIndex;
						}
						else
						{
							reqLength = metaInformation.info_piece_length;
						}
						int begin = 0;
						pieceIndex = htonl(pieceIndex);
						begin = htonl(begin);
						reqLength = htonl(reqLength);
						memcpy(payloadToSend,&pieceIndex,4);
						memcpy(&payloadToSend[4],&begin,4);
						memcpy(&payloadToSend[8],&reqLength,4);
						bytesWritten = SendMessage(fd, 6, 13, payloadToSend,1);
						if (bytesWritten < 0)
						{
							printf("Error sending request message\n");
						}
						else 
						{
							//printf("Sending next request for piece: %d\n",ntohl(pieceIndex));
							leecherRqstTimer = time(NULL);
						}
						//sleep(1);
					}
				}
			}
			FD_ZERO(&monitoring_set);
			FD_SET(fd, &monitoring_set);
			ChangedFDs = select(MaxDescriptor, &monitoring_set, NULL, NULL,&leecher_tv);
			if (FD_ISSET(fd,&monitoring_set))
			{
				ReadMessage(fd, pIndex, cIndex, readLen8, 4);
				memcpy(&readLen,readLen8,4);
				readLen = ntohl(readLen);
				if (readLen != 0) 
				{
					ReadMessage(fd, pIndex, cIndex, &msgID, 1);
				}
				switch (msgID) {
					case CHOKE:
						connectedClients_Info[cIndex].peer_choking = 1;
						break;
					case UNCHOKE:
						connectedClients_Info[cIndex].peer_choking = 0;
						// Find a piece to request
						int pieceIndex = RequestPiece(cIndex);
						// If peer does not have piece we are not interested
						if (pieceIndex < 0)
						{
							SendMessage(fd, 3, 1, NULL, 0);
						}
						// Send request for piece
						else
						{
							int reqLength;
							// Last piece may not be of the full piece size
							if ((pieceIndex+1) == numPieces)
							{
								reqLength = metaInformation.file_info_length - metaInformation.info_piece_length*pieceIndex;
							}
							else
							{
								reqLength = metaInformation.info_piece_length;
							}
							int begin = 0;
							pieceIndex = htonl(pieceIndex);
							begin = htonl(begin);
							reqLength = htonl(reqLength);
							memcpy(payloadToSend,&pieceIndex,4);
							memcpy(&payloadToSend[4],&begin,4);
							memcpy(&payloadToSend[8],&reqLength,4);
							bytesWritten = SendMessage(fd, 6, 13, payloadToSend,1);
							if (bytesWritten < 0)
							{
								printf("Error sending request message\n");
							}
							else 
							{
								leecherRqstTimer = time(NULL);
//								printf("Sending Initial Piece Request: %d\n",ntohl(pieceIndex));
							}
							
						}
						break;
					case INTERESTED:
						connectedClients_Info[cIndex].peer_interested = 1;
						SendMessage(fd, 1, 1, NULL,0);
						connectedClients_Info[cIndex].am_choking = 0;
						break;
					case NOTINTERESTED:
						connectedClients_Info[cIndex].peer_interested = 0;
						break;
					case HAVE:
						ReadMessage(fd, pIndex, cIndex, payloadRecv, readLen-1);
						memcpy(&pieceIndex,payloadRecv,4);
						pieceIndex = ntohl(pieceIndex);
						UpdatePeerBitfield(cIndex, pieceIndex);
						break;
					case BITFIELD:
						// We will read bitfield messages, but will not process them.
						// Read message payload
						ReadMessage(fd, pIndex, cIndex, peerBitField, readLen-1);
						break;
					case REQUEST:
						ReadMessage(fd, pIndex, cIndex, payloadRecv, 12);
						memcpy(&pieceIndex,payloadRecv,4);
						memcpy(&begin,&payloadRecv[4],4);
						memcpy(&reqLength,&payloadRecv[8],4);
						pieceIndex = ntohl(pieceIndex);
						begin = ntohl(begin);
						reqLength = ntohl(reqLength);

//						printf("Piece Requested: %d\n",pieceIndex);
//						printf("Begin Request Position: %d\n",begin);
//						printf("Length of Request: %d\n",reqLength);
						int bytes_read =  FileReader(pieceIndex, begin, reqLength, pieceData);
  						if (bytes_read < 0)
  						{
  							//printf("Peer attampted to read past the EOF.\n");
  						}
  						else
  						{
							uploaded = uploaded + bytes_read;
							UpdateUploaded(bytes_read);
							//printf("Bytes Read: %d\n",bytes_read);
  							pieceIndex = htonl(pieceIndex);
  							begin = htonl(begin);
  							memcpy(payloadToSend,&pieceIndex,4);
  							memcpy(&payloadToSend[4],&begin,4);
  							memcpy(&payloadToSend[8],pieceData,bytes_read);
  							bytesWritten = SendMessage(fd,7, 9+bytes_read, payloadToSend,1);
							if (bytesWritten < 0)
							{
								printf("Error sending piece message\n");
							}
							else
							{
								//printf("Sending over piece: %d\n",ntohl(pieceIndex));
							}
							
							
  						}
						break;
					case PIECE:
						ReadMessage(fd, pIndex, cIndex, payloadRecv, readLen-1);
 						memcpy(&pieceIndex,payloadRecv,4);
  						memcpy(&begin,&payloadRecv[4],4);
  						memcpy(pieceData, &payloadRecv[8],readLen - 9);
						pieceIndex = ntohl(pieceIndex);
						begin = ntohl(begin);
 						//printf("Received Piece: %d\n",pieceIndex);
  						//printf("Beginning position in piece: %d\n",begin);
						int bytes_written = FileWriter(pieceIndex, begin, pieceData, readLen-9);
						if (bytes_written < 0)
						{
							//printf("Attempting to write past EOF.\n");
						}
						else
						{
							//printf("Wrote %d bytes into file\n", readLen-9);
							downloaded = downloaded + (readLen-9);
							UpdateDownloaded(readLen-9);
							VerifySeederStatusVsBitField();
							//printf("Seeder Status: %d\n",isSeeder);
							if (isSeeder == 1)
							{
								char recv_line[FiveTwelve] = {'\0'};
								char trackerResponse[FiveTwelve] = {'\0'};
								GetTracker(COMPLETETRACKER,recv_line,trackerResponse);
								printf("*****EXCELLENT...DOWNLOAD COMPLETED!!!!******\n");
							}
							leecherRqstTimer = 0;
							thisBitField[pieceIndex] = 1;
							SendGlobalHaveMessage(pieceIndex);
						}
						break;
					case CANCEL:
						break;
				}
			}
		}
		//memset(recv_line,0,sizeof(recv_line));
		//printf("Sittin' here restin' ma bones!\n");
		//sleep(5);
	}
	
}
// Makes sure AcceptConnectionHandler and AnnounceConnection Handler
// are not accessing the struct arguments at the same time.
void PeerThreadFactory(int pIndex, int cIndex, int fd, struct sockaddr_in * addr) 
{
	pthread_mutex_lock (&mutexArgs);
	
	arguments[cIndex].conIndex = cIndex;
	arguments[cIndex].potIndex = pIndex;
	arguments[cIndex].someFD = fd;
	arguments[cIndex].someSock = (struct sockaddr_in) *addr;
	pthread_create(&workerThreads[cIndex],NULL,PeerThread,(void *) &arguments[cIndex]);
	
	pthread_mutex_unlock (&mutexArgs);
	return;
}

//Will handle connection accept from a peer
void *AcceptConnectionHandler()
{
	struct sockaddr_in incoming_addr;
	memset((void *) &incoming_addr, 0, sizeof(incoming_addr));
	socklen_t addr_size;
	int new_fd = -1, full = 1, index = -1;
	
	while (1) 
	{
		addr_size = sizeof(incoming_addr);
		if ((new_fd = accept(TCP_Listening_FD, (struct sockaddr *) &incoming_addr, &addr_size)) < 0) {
			printf("bad accept\n");
			if (errno == EINTR) {
				continue;  /* restart accepting new connections */
			} else {     /* otherwise, some error has occured */
				sys_error("accept()");
			}
		}
		else
		{
			index = CheckAvailableConnection(new_fd);
			if (index < 0)
			{
				printf("You have reached the maximum allowable connections.\n");
			}
			else
			{
				PeerThreadFactory(-1,index, new_fd,&incoming_addr);
			}
		}
		new_fd = -1;
		memset((void *) &incoming_addr, 0, sizeof(incoming_addr));
	}
	pthread_exit(0);
}

//Will handle a connection made from us to another peer
void *AnnounceConnectionHandler()
{
	int new_fd = -1;
	char peerIP[INET_ADDRSTRLEN] = {'\0'};
	char connectedIP[INET_ADDRSTRLEN] = {'\0'};
	memset(peerIP,0,sizeof(peerIP));
	memset(connectedIP,0,sizeof(connectedIP));
	int peerPort = 0, connectedPort = 0, isSame = 0, index = -1;
	time_t start = time(NULL);
	while (1) 
	{
		for (int i = 0; i < MAXPOTENTIALPEERS; i++)
		{
			if (potentialPeers_Info[i].isConnected == 0)
			{
				inet_ntop(AF_INET,&(potentialPeers_Info[i].potential_sock.sin_addr),peerIP,INET_ADDRSTRLEN);
				peerPort = ntohs(potentialPeers_Info[i].potential_sock.sin_port);
				for (int j = 0; j < MAXCONNECTEDCLIENTS; j++)
				{
					if (connectedClients_Info[j].connection_fd != -1)
					{
						inet_ntop(AF_INET,&(connectedClients_Info[i].connection_sock.sin_addr),connectedIP,INET_ADDRSTRLEN);
						connectedPort = ntohs(connectedClients_Info[i].connection_sock.sin_port);
						if (strcmp(connectedIP,peerIP) == 0 && connectedPort == peerPort)
						{
							isSame = 1;
							potentialPeers_Info[i].isConnected = 1;
							memset(connectedIP,0,sizeof(connectedIP));
							connectedPort = 0;
							break;
						}
					}
				}
			}
			if ((isSame == 0) && (potentialPeers_Info[i].isConnected == 0) && (potentialPeers_Info[i].potential_fd != -1) && !((peerPort == thisPort) && ((strcmp(peerIP,thisIP) == 0) || (strcmp(peerIP,"127.0.0.1") == 0))))
			{
				//Use ConnectTCP() function
				new_fd = ConnectTCP(&potentialPeers_Info[i].potential_sock);
				if (new_fd < 0)
				{
					printf("bad connection to: %s on port: %d\n",peerIP,ntohs(potentialPeers_Info[i].potential_sock.sin_port));
					continue;
				}
				else
				{
					index = CheckAvailableConnection(new_fd);
					if (index == -1) 
					{
						printf("You have reached the maximum allowable connections.\n");
					}
					else
					{
						potentialPeers_Info[i].isConnected = 1;
						PeerThreadFactory(i,index,new_fd,&potentialPeers_Info[i].potential_sock);
					}
				}
			}
			sleep(1);
		}
		memset(peerIP,0,sizeof(peerIP));
		peerPort = 0;
		isSame = 0;
		index = -1;
		sleep(3);
		// Send a request to tracker after the min interval has expired
		if (((time(NULL) - start) > trackerInfo.min_interval) && trackerInfo.min_interval > 0)
		{
			char recv_line[FiveTwelve] = {'\0'};
			char trackerResponse[FiveTwelve] = {'\0'};
			GetTracker(-1,recv_line,trackerResponse);
			if (trackerResponse[9] == '2') 
			{
				DecodeTrackerResponse(recv_line);
				close(trackerFD);
			}
			else
			{
				printf("There was an error connecting to tracker.\n");
			}
			start = time(NULL);
		}
	}
	pthread_exit(0);
}

//Prints the prompt
void prompt()
{
	printf("\n>");
	fflush(stdout);
}

int main (int argc, const char * argv[]) {
	//Supplied the correct number of arguments?
	if (argc != 3) 
	{
		printf("Too many or not enough arguments supplied.\n");
		printf("The arguments are <Torrent File Path> <Port Number>\n");
		return 1;
	}
	// Ignore SIGPIPES
	new_action.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &new_action, NULL);
	
	memset(pieceField,-1,BitFieldLength);
	//Generate Pseudorandom ID
	GenerateID();
	
	strcpy(inputFile,argv[1]);
	//Get the torrent file
	//Get the torrent file
	if(strstr(inputFile, ".torrent") == NULL)
	{
		 printf("The input file must be a valid .torrent file.");
		 return 1;
	}
	BendcodeFile(inputFile);
	VerifyFile();
	pthread_mutex_init(&readMutex, NULL);
	pthread_mutex_init(&writeMutex, NULL);
	
	// Create a bitfield array with zero padding up to 8 bits
	// If there is only 1 piece, then the array will buffer 7 zeros in the thisBitField array
	memcpy(thisBitField,pieceField,BitFieldLength);
	numPieces = 0;
	while(1)
	{
		if (pieceField[numPieces] == -1)
		{
			break;
		}
		numPieces++;
	}
	for (int x = 0; x < BitFieldLength; x++) 
	{
		thisBitField[x] = -1;
	}
	bufferToByteSize = ceil((double)numPieces/8);
	for (int x = 0; x < bufferToByteSize*8; x++)
	{
		if (pieceField[x] == 1)
		{
			thisBitField[x] = 1;
		}
		else
		{
			thisBitField[x] = 0;
		}
	}
	lastPieceSize = metaInformation.file_info_length - ((numPieces-1) * metaInformation.info_piece_length);
	
	//OK TCP port number?
	thisPort = atoi(argv[2]);
	if (thisPort <= 0 || thisPort >= 65535) 
	{
		sys_error("An invalid TCP port number was entered.  Valid ports are from 0 to 65535.");
	}
	
	//Alright it looks good from here..
	//Lets check to see if we can open the TCP port...
	
	if ( (TCP_Listening_FD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		sys_error("A TCP File Descriptor could not be created...");
	}
	
	/*memset((void *) &thisClientAddress, 0, sizeof(thisClientAddress));
	thisClientAddress = GetIPAddress();
	thisClientAddress.sin_family      = AF_INET;
	inet_ntop(AF_INET,&(thisClientAddress.sin_addr),thisIP,INET_ADDRSTRLEN);
	*/
	memset((void *) &thisClientAddress, 0, sizeof(thisClientAddress));
	thisClientAddress = GetIPAddress();
	thisClientAddress.sin_family      = AF_INET;
	inet_ntop(AF_INET,&(thisClientAddress.sin_addr),thisIP,INET_ADDRSTRLEN);
	thisClientAddress.sin_port        = htons(thisPort);
	
	if (setsockopt(TCP_Listening_FD, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		sys_error("There was an error setting the options on the TCP socket.");
	}
	
	// bind server_address to the socket
	if (bind(TCP_Listening_FD, (struct sockaddr *) &thisClientAddress, sizeof(thisClientAddress)) < 0) {
		sys_error("There was an error binding to the socket.  Ensure that the port is free.");
	} 
	
	// actually turn the socket into a passive socket
	if (listen(TCP_Listening_FD, backlog) < 0) {
		sys_error("There was an error listening to the socket.  Ensure that the port is free.\n");
	}
	
	// Initialize peer FD's
	for (int i = 0; i < MAXCONNECTEDCLIENTS; i++)
	{
		connectedClients_Info[i].connection_fd = -1;
	}
	// Initialize potential peers checker array
	for (int i = 0; i < MAXPOTENTIALPEERS; i++)
	{
		potentialPeers_Info[i].isConnected = -1;
		
	}
	for (int i = 0; i < MAXCONNECTEDCLIENTS; i++)
	{
		arguments[i].someFD = -1;
		arguments[i].conIndex = -1;
		arguments[i].potIndex = -1;
	}
	
	// Create our workhorse thread
	if (pthread_create(&announceThread,NULL,AnnounceConnectionHandler,NULL) < 0)
	{
		sys_error("Houston, we have a thread problem.\n");
	}
	
	// Create our workhorse thread
	if (pthread_create(&acceptThread,NULL,AcceptConnectionHandler,NULL) < 0)
	{
		sys_error("Houston, we have a thread problem.\n");
	}
	
	if (pthread_create(&optimisticThread,NULL,OptimisticUnchoke,NULL) < 0)
	{
		sys_error("Houston, we have a thread problem.\n");
	}		
	
	// Initialize our tracker socket
	memset((void *) &trackerAddress, 0, sizeof(trackerAddress));
	trackerAddress.sin_family = AF_INET;

	printf("UBTorrent has started up successfully!\n");
	printf("This UBTorrent application was created by:\n");
	printf("Justin Domes : justindo@buffalo.edu \n");
	printf("Eric Nagler : ednagler@buffalo.edu \n");
	// Begin scanning for console input
	while (1)
	{
		prompt();
		scanf("%s",consoleInput);
		ProcessConsoleInput(consoleInput);
		memset(consoleInput,'\0',TORRENTLENGTH);
	}
    return 0;
}
