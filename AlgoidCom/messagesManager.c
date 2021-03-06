
//#define ADDRESS     "192.168.3.1:1883"
#define ADDRESS     "localhost:1883"

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "pthread.h"
#include "messagesManager.h"
#include "mqttProtocol.h"
#include "linux_json.h"
#include "udpPublish.h"

// Thread Messager
pthread_t th_messager;

char BroadcastID[50]="mgr_";
char ClientID[50]="mgr_";


void sendMqttReport(int msgId, char * msg);

int  mqttMsgArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message);
void mqttConnectionLost(void *context, char *cause);

void sendResponse(int msgId, char * msgTo, unsigned char msgType, unsigned char msgParam, unsigned char valCnt);
int pushMsgStack(void);
int pullMsgStack(unsigned char ptrStack);
char clearMsgStack(unsigned char ptrStack);

// Initialisation des variables
unsigned char mqttDataReady=0;
int mqttStatusErr;

char MqttDataBuffer[500];
char msgReportBuffer[100];

// Initialisation principale du system de messagerie
void *MessagerTask (void * arg){	 													// duty cycle is 50% for ePWM0A , 25% for ePWM0B;
	int lastMessage;
	int i;

	// Initialisation de la pile de reception de message
	for(i=0;i<10;i++)
		clearMsgStack(i);

	// Creation d'un id unique avec l'adresse mac
	sprintf(&ClientID[4], "%s", getMACaddr());
	// Connexion au broker MQTT
	mqttStatusErr=mqtt_init(ADDRESS, ClientID, mqttMsgArrived, mqttConnectionLost);
	if(!mqttStatusErr){
		printf("#[MSG MANAGER] Connection au broker MQTT: OK (IP: %s avec ID: %s)\n", ADDRESS, ClientID);
		if(!mqttAddRXChannel(TOPIC_MANAGER)){
			printf("#[MSG MANAGER] Inscription au topic: OK\n");
                        sendMqttReport(-1, "IS NOW ONLINE");
		}
		else {
			printf("#[MSG MANAGER] Inscription au topic: ERREUR\n");
		}
	}else {
                
		printf("#[MSG MANAGER] Connexion au broker MQTT: ERREUR\n");
                
	}
// BOUCLE PRINCIPALE
	while(1)
	{
            // Try to reconnect to brocker
            if(mqttStatusErr){
                // Connexion au broker MQTT
                mqttStatusErr=mqtt_init(ADDRESS, ClientID, mqttMsgArrived, mqttConnectionLost);
                if(!mqttStatusErr){
                        printf("#[MSG MANAGER] Connection au broker MQTT: OK (IP: %s avec ID: %s)\n", ADDRESS, ClientID);
                        if(!mqttAddRXChannel(TOPIC_MANAGER)){
                                printf("#[MSG MANAGER] Inscription au topic: OK\n");
                                sendMqttReport(-1, "IS NOW ONLINE");
                        }
                        else {
                                printf("#[MSG MANAGER] Inscription au topic: ERREUR\n");
                        }
                }else {
                        printf("#[MSG MANAGER] Connexion au broker MQTT: ERREUR\n");
                }
                sleep(5);
            }
            
	    // Verification de l'arriv�e d'un message MQTT
	    if(mqttDataReady){
	    // RECEPTION DES DONNES UTILES
                if(GetAlgoidMsg(AlgoidMessageRX, MqttDataBuffer)>0){
                        // Contr�le du destinataire
                        if(!strcmp(AlgoidMessageRX.msgTo, ClientID) || !strcmp(AlgoidMessageRX.msgTo, BroadcastID)){
                                // Enregistrement du message dans la pile
                                lastMessage=pushMsgStack();
                                if(lastMessage>=0){
                                                                       
                                        // Retourne un ack a l'expediteur
                                        sendResponse(AlgoidMessageRX.msgID, AlgoidMessageRX.msgFrom, ACK, AlgoidMessageRX.msgParam, 0);
                                        sprintf(msgReportBuffer, "%s", ClientID);
                                        sendMqttReport(-1, "New message received");
                                }
                                else{
                                        printf("ERROR: Message stack full !\n");
                                        sendMqttReport(-1, "ERROR: Message stack full !");
                                        
                                }
                        }
                        else{
                                printf("IGNORE: bad destination name\n");
                                sendMqttReport(-1, "IGNORE: bad destination name");
                        }
                        
                }else{
                        // Retourne une erreur a l'expediteur
                        sendResponse(AlgoidMessageRX.msgID, AlgoidMessageRX.msgFrom, AlgoidMessageRX.msgType, AlgoidMessageRX.msgParam, 0);
                        printf("\nERROR: Incorrect message format\n");
                        sprintf(msgReportBuffer, "%s", ClientID);
                        sprintf(&msgReportBuffer[8], " -> %s", "ERROR: Incorrect message format");
                        sendMqttReport(AlgoidMessageRX.msgID, msgReportBuffer);
                        
                }
                mqttDataReady=0;
            }
            
            usleep(10000);
        }
 // FIN BOUCLE PRINCIPAL

  usleep(5000);
  pthread_exit (0);
}



int pushMsgStack(void){
	int ptrMsgRXstack=0;

	// Recherche un emplacement vide dans la pile
	for(ptrMsgRXstack=0;(ptrMsgRXstack<10) && (AlgoidMsgRXStack[ptrMsgRXstack].msgID>=0);ptrMsgRXstack++);

	// CONTROLE DE L'ETAT DE LA PILE DE MESSAGE
	if(ptrMsgRXstack>=10){
		return -1;
	}else{
		// ENREGISTREMENT DU MESSAGE DANS LA PILE
		AlgoidMsgRXStack[ptrMsgRXstack]=AlgoidMessageRX;

		ptrMsgRXstack++;
		return ptrMsgRXstack-1;
	}
}

int pullMsgStack(unsigned char ptrStack){
		int i;
		unsigned char result;

		if(AlgoidMsgRXStack[ptrStack].msgType!=-1){
			AlgoidCommand=AlgoidMsgRXStack[ptrStack];

			// Contr�le le ID, FROM, TO du message et creation g�n�rique si inexistant
			if(AlgoidCommand.msgID <= 0){
				AlgoidCommand.msgID = rand() & 0xFFFFFF;
			}


			if(!strcmp(AlgoidCommand.msgFrom, "")){
				strcpy(AlgoidCommand.msgFrom,"unknown");
			}

			// D�place les elements de la pile
			for(i=ptrStack;i<9;i++){
				AlgoidMsgRXStack[ptrStack]=AlgoidMsgRXStack[ptrStack+1];
				ptrStack++;
			}

			// EFFACE LES DONNEES DE LA PILE
			strcpy(AlgoidMsgRXStack[9].msgFrom, "");
			strcpy(AlgoidMsgRXStack[9].msgTo, "");
			AlgoidMsgRXStack[9].msgID=-1;
			AlgoidMsgRXStack[9].msgParam=-1;
			AlgoidMsgRXStack[9].msgType=-1;
			AlgoidMsgRXStack[9].msgValueCnt=0;

			for(i=0;i<AlgoidMsgRXStack[9].msgValueCnt;i++){

			}

			return 1;
		}else
			return 0;
}
// ----------------------

char clearMsgStack(unsigned char ptrStack){
		int i;

		if(ptrStack<10){
			// EFFACE LES DONNEES DE LA PILE
			strcpy(AlgoidMsgRXStack[ptrStack].msgFrom, "");
			strcpy(AlgoidMsgRXStack[ptrStack].msgTo, "");
			AlgoidMsgRXStack[ptrStack].msgID=-1;
			AlgoidMsgRXStack[ptrStack].msgParam=-1;
			AlgoidMsgRXStack[ptrStack].msgType=-1;

			for(i=0;i<AlgoidMsgRXStack[ptrStack].msgValueCnt;i++){
				//strcpy(AlgoidMsgRXStack[ptrStack].DCmotor[i].motor, "");
			}
			return 0;
		}
		return 1;
}

// ------------------------------------------------------------------------------------
// INITMESSAGER: Initialisation du gestionnaire de message
// - Demarrage thread messager
// -
// ------------------------------------------------------------------------------------
int InitMessager(void){
	// CREATION DU THREAD DE MESSAGERIE (Tache "MessagerTask")
	  if (pthread_create (&th_messager, NULL, MessagerTask, NULL)!= 0) {
		  //Connexion au brocker MQTT

		return (1);
	  }else return (0);

}

// ------------------------------------------------------------------------------------
// CLOSEMESSAGE: Fermeture du gestionnaire de messages
// - Fermeture MQTT
// - Stop le thread manager
// ------------------------------------------------------------------------------------
int CloseMessager(void){
	int result;
	// TERMINE LE THREAD DE MESSAGERIE
	pthread_cancel(th_messager);
	// Attends la terminaison du thread de messagerie
	result=pthread_join(th_messager, NULL);
	return (result);
}


// -------------------------------------------------------------------
// Fonction Call-Back reception d'un message MQTT
// -------------------------------------------------------------------
int mqttMsgArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    unsigned int i;
    char* payloadptr;
    int messageCharCount=0;

    payloadptr = message->payload;
    messageCharCount=message->payloadlen;

    for(i=0; i<messageCharCount; i++)
    {
    	MqttDataBuffer[i]=payloadptr[i];
    }

    // Termine la chaine de caract�re
    MqttDataBuffer[messageCharCount]=0;

	mqttDataReady=1;

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

// -------------------------------------------------------------------
// Fonction Call-Back en cas de perte de connexion MQTT
// -------------------------------------------------------------------
void mqttConnectionLost(void* context, char* cause)
{
    printf("#[MSG MANAGER] Perte de connexion avec le broker MQTTS\n");
    mqttStatusErr = 1;
}


// -------------------------------------------------------------------
// SENDRESPONSE
// Retourne un message MQTT
// -------------------------------------------------------------------

void sendResponse(int msgId, char * msgTo, unsigned char msgType, unsigned char msgParam, unsigned char valCnt){
	char MQTTbuf[MAX_MQTT_BUFF];
	char ackType[15], ackParam[15];
	char topic[50];
        
	// G�n�ration du texte de reponse TYPE pour message MQTT et selection du topic de destination
	switch(msgType){
		case COMMAND : strcpy(ackType, "command"); strcpy(topic, TOPIC_MANAGER); break;			// Commande vers l'h�te ****** NON UTILISE **********
		case REQUEST : strcpy(ackType, "request"); strcpy(topic, TOPIC_MANAGER); break;			// Requ�te vers l'h�te ****** NON UTILISE **********
		case ERR_TYPE : strcpy(ackType, "error"); break;
		default : strcpy(ackType, "unknown"); break;
	}

// G�n�ration du texte de reponse TYPE pour message MQTT
	switch(msgParam){
		case ERR_PARAM : strcpy(ackParam, "error"); break;
                case CONFIG : strcpy(ackParam, "config"); break;
                case SYSTEM : strcpy(ackParam, "system"); break;
		default : strcpy(ackParam, "unknown"); break;
	}

	ackToJSON(MQTTbuf, msgId, msgTo, ClientID, ackType, ackParam, msgParam, valCnt);
	mqttPutMessage(&topic, MQTTbuf, strlen(MQTTbuf));
}

// -------------------------------------------------------------------
// SENDMQTTREPORT
// Retourne un message MQTT
// -------------------------------------------------------------------

void sendMqttReport(int msgId, char * msg){
	char MQTTbuf[1024];

	// Creation d'un id unique avec l'adresse mac
	sprintf(&MQTTbuf[0], "%s -> Message ID: %d -> %s", ClientID, msgId, msg);
	mqttPutMessage(TOPIC_DEBUG, MQTTbuf, strlen(MQTTbuf));
}






