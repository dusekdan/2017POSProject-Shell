#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE 500
#define _XOPEN_SOURCE_EXTENDED 1

#define BUFFER_SIZE 513
#define BUFFER_LAST_INDEX 512

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h> /* read and write functions */
#include <sys/wait.h>
#include <fcntl.h>

#include <errno.h>

#define DEBUG 0

void *readInput(void * data);
void *executeCommand(void * data);

void printShellHud();
void flushStdin();

char* sharedBuffer;

pthread_mutex_t bufferAccessMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t dummyMutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t dummyMutex2 = PTHREAD_MUTEX_INITIALIZER;



pthread_cond_t bufferReady1 = PTHREAD_COND_INITIALIZER;
pthread_cond_t bufferReady2 = PTHREAD_COND_INITIALIZER;

/**
 * Application entry point
 * Starts shell loop thread 
 */
int main (int argc, char** argv)
{
	pthread_t readThread;
	pthread_t executeThread;

	/* Shared buffer pre-allocation */
	sharedBuffer = (char*)  malloc (BUFFER_SIZE * sizeof (char));

	pthread_create(&readThread, NULL, readInput,  (void *)(intptr_t)(1));
	pthread_create(&executeThread, NULL, executeCommand,  (void *)(intptr_t)(2));



	/* Clean up threads */
	pthread_join (readThread, NULL);
	pthread_join (executeThread, NULL);

	/* Clean up mutexes */
	pthread_mutex_destroy(&bufferAccessMutex);
	return 0;
}

void *readInput(void * data)
{
	/*long tid = (intptr_t) data;	For now passing only ID */

	int inputLength;
	char inputData[BUFFER_SIZE];

	/* As long as program is running, read commands */
	while (1)
	{
		printShellHud();	/* Displays ddsh> for user and flushes stdout */
		
		memset(inputData, '\0', BUFFER_SIZE);

		inputLength = read(0, inputData, 513);
		
		/* Required null termination for every input string */
		inputData[512] = '\0';


		/* 
			Note that also '\n' character is read - you can enter only 511 characters, 512th will be \n (and should  be) 
			Task assignment is very specific on how the input should be read though, so this is the correct way.
		*/
		if (inputLength == 513)	
		{
			fprintf (stderr, "I'm not your friend, mate. Input is too long (MAX_BUFF_SIZE=512)\n");
			flushStdin();
			continue;
		}

		/*
			Get rid of just empty line hits and 
		*/



		/* Right here I have string from input read to inputData variable, and I will copy it to sharedBuffer (mutex lock required) */
		pthread_mutex_lock(&bufferAccessMutex);
		memset(sharedBuffer, '\0', BUFFER_SIZE);
		memcpy(sharedBuffer, inputData, inputLength);
		pthread_mutex_unlock(&bufferAccessMutex);

		/* Tell thread #2 that sharedBuffer is ready */
		pthread_cond_signal(&bufferReady1);

		/* Wait for execution thread to do its work */
		printf("DRead vlákno čeká na signál.\n");
		pthread_cond_wait(&bufferReady2, &dummyMutex1);

		/* Terminate loop when user tries to exit, strcspn count number of character before it hits \r\n || \n || \0...*/
		/*if (strcmp(inputData[strcspn(inputData, "\r\n")], "exit") == 0)
			break; // TODO: Move this to execute routine*/

		/* ALL THAT HAPPENS HERE IS HAPPENING AFTER EXECUTIONER DOES HIS WORK */

		/*printf("Vypisuji: ");
		printf ("%s", sharedBuffer);
		printf("\n");*/
	}
	

	/*printf("[R] Exited");*/
	pthread_exit(NULL);
}



/**
 *	Executes command from sharedBuffer
 */
void *executeCommand(void * data)
{

	while (1)
	{
		char* outFDName;
		char* inFDName;
		int   outFD;
		int   inFD;

		char* programName = NULL;
		char** arguments = NULL;
		char** tempArgs = NULL;

		char* argument = NULL;
		char* tmpArgument = NULL;

		char *token, *string;

		int firstparam = 1;
		int argcout = 1;
	    int isFileName = 0;

		/* 
			Wait until signaled by reading thread, then acquire mutex 
			Signaling thread should unlock bufferMutex before signaling
		*/
		printf("D: Execute vlákno čeká na signál.\n");
		pthread_cond_wait(&bufferReady1, &dummyMutex2);

		/* Get rid of trailing \n */
		if (strlen(sharedBuffer) > 1)
			sharedBuffer[strlen(sharedBuffer)-1] = 0;

		/* Handle exit command */
		if (strcmp(sharedBuffer, "exit") == 0)
		{
			printf ("Exiting...\n");
			exit (0);
		}
		
		printf ("E: Received signal, buffer-contents: %s\n", sharedBuffer);

		/* 
		Standard case - only ./program [anything] || nothing cases are handled here
		And cases where < and > are SEPARATED BY SPACE from other text
		Note: I am not proud of this way of coding, but ...time. 
	    */
		string = strdup(sharedBuffer);
		token = strtok(string, " ");
			
		while (token != NULL)
		{
			if (firstparam)
			{
				programName = strdup(token);
				firstparam = 0;
			}

			/* Also maybe handle > and < */
			if (strlen(token) == 1 && strcmp(token, ">") == 0)
			{
				printf("\nWATCH OUT, FILE NAME TO BE EXPECTED\n");
				/* Raise flag and continue */
				isFileName = 1;
				token = strtok(NULL, " ");
				continue;
			}
			else if (strlen(token) == 1 && strcmp(token, "<") == 0)
			{
				printf("\nInput filename to be expected\n");
				/* Raise flag and continue */
				isFileName = 2;
				token = strtok(NULL, " ");
				continue;
			}

			if (isFileName == 1)
			{

				/* Reset flag back and jump to forking part with proper dup() call*/
				outFDName = (char*) malloc(strlen(token)*sizeof(char));
				memcpy(outFDName, token, strlen(token));
				printf("FILENAME: %s\n",  outFDName);
				token = NULL;
				isFileName = 1;
				continue;
			}
			else if (isFileName == 2)
			{

				/* Reset flag back and jump to forking part with proper dup() call*/
				inFDName = (char*) malloc(strlen(token)*sizeof(char));
				memcpy(inFDName, token, strlen(token));
				printf("FILENAME: %s\n", inFDName);
				token = NULL;
				isFileName = 2;
				continue;
			}
		
			tempArgs = (char**) realloc(arguments, (sizeof(char *) * argcout));
			/*memset(tempArgs, '\0', sizeof(char*)*argcout);*/
			arguments = tempArgs;

			tmpArgument = (char*) realloc(argument, (strlen(token)+1) * sizeof(char));
			argument = tmpArgument;
			
			strcpy(argument, token);
			argument[strlen(argument)] = '\0';

			arguments[argcout-1] = strdup(argument);

			if (DEBUG)
				printf("Also need to incorporate param: %s \n", token);

			argcout++;

			token = strtok(NULL, " ");
		}

		/* Add last null-terminated arg to arguments */
		tempArgs = (char**) realloc(arguments, sizeof(char*) * argcout);
		arguments = tempArgs;
		tmpArgument = (char*) realloc(argument, (sizeof(char*)));
		argument = tmpArgument;
		argument = '\0';
		arguments[argcout-1] = argument;

		if (arguments != NULL)
		{
			/* Call exec with args*/
			pid_t child = fork();
			if (child == 0)
			{

				/* Set output file accordingly */
				if (isFileName == 1)
				{
					outFD = open(outFDName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
					if (outFD < 0)
					{
						fprintf (stderr, "Unable to open output file.\n");
					}

					/* Point stdout(1) to outFD */
					if (dup2(outFD, 1) < 0)
					{
						perror("Failed to dup2(fd, stdout).\n");
					}
				}

				/* Set input file accordingly */
				if (isFileName == 2)
				{
					inFD = open(inFDName, O_RDONLY, 0666);
					if (inFD < 0)
					{
						perror("Opening input file failed.");
					}

					if (dup2(inFD, 0) < 0)
					{
						perror("Opening output file failed.");
					}
				}

				if (execvp(programName, arguments) < 0) 
				{
					int err = errno;
					if (err == 2)
					{
						fprintf(stderr, "Command not supported.\n");
					}
					else
					{
						printf("Error: %d", err);
					}
					exit(1);
				}


				exit(0);
			}
			wait(NULL);
		}
		


		


		/* Releasing mutex & signaling the other thread */
		pthread_cond_signal(&bufferReady2);
	}

	pthread_exit(NULL);
}

/**
 * Displays shell message and flushes stdout
 */
void printShellHud()
{
	printf ("ddsh> ");
	fflush(stdout);
}

/**
 * Flushes input buffer, so the next line can be read
 * Code inspired by http://stackoverflow.com/questions/2187474
 * Note that according to Linux fpurge man page: Usually it is 
 * mistake to want to discard input buffers.
 * TODO: Verify this is not causing any side effects (command not found when more than 511 characters are input?)
 * TODO: Fix the loop, it does not seem to recognize end of the loop correctly when left as it is
 */
void flushStdin()
{
	int c;
	while ((c = getchar()) != '\n');
	/*int c;
	while ((c = getchar()) != '\n' ||  c != EOF);	Loops until EOL or EOF is hit 
	*/
}