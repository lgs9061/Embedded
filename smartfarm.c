#include<wiringPi.h>
#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<sys/types.h>
#include<unistd.h>
#include<pthread.h>
#include<mysql/mysql.h>
#include<sys/types.h>
#include<errno.h>
#include<wiringPiSPI.h>
#include<string.h>
#include<signal.h>

#define MAX 5
#define MAXTIMINGS 85

#define CS_MCP3208 24
#define SPI_CHANNEL 0
#define SPI_SPEED 1000000

#define FAN 22
#define RGBLEDPOWER 24
#define LIGHTSEN_OUT 2
#define RED 27
#define BLUE 29
#define GREEN 28

#define DBHOST "192.168.1.111"
#define DBUSER "root"
#define DBPASS "root"
#define DBNAME "demofarmdb"

MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;
int temp_cur=0;
int buffer[MAX];
int buffer1[MAX];
int fill_ptr1=0;
int use_ptr1=0;
int count1=0;
int fill_ptr=0;
int use_ptr=0;
int count =0;
int fanflag=0;

pthread_cond_t empty,fill,fill1,fill2;
pthread_mutex_t mutex;

void Bpluspinmodeset(void){
	pinMode(LIGHTSEN_OUT,INPUT);
	pinMode(FAN, OUTPUT);
	pinMode(RGBLEDPOWER,OUTPUT);
	pinMode(RED, OUTPUT);
	pinMode(BLUE,OUTPUT);
	pinMode(GREEN, OUTPUT);
}
int ret_temp;
static int DHTPIN = 11;
static int dht22_dat[5]={0,0,0,0,0};
static uint8_t sizecvt(const int read){
	if(read>255 || read<0){
		printf("Invalid data from wiringPi library\n");
		exit(EXIT_FAILURE);
	}
	return (uint8_t)read;
}
int read_dht22_dat(){
	uint8_t laststate = HIGH;
	uint8_t counter =0;
	uint8_t j=0,i;

	dht22_dat[0]=dht22_dat[1]=dht22_dat[2]=dht22_dat[3]=dht22_dat[4]=0;

	pinMode(DHTPIN, OUTPUT);
	digitalWrite(DHTPIN, HIGH);
	delay(10);
	digitalWrite(DHTPIN,LOW);
	delay(18);
	digitalWrite(DHTPIN,HIGH);
	delayMicroseconds(40);
	pinMode(DHTPIN, INPUT);

	for(i=0;i<MAXTIMINGS;i++){
		counter=0;
		while(sizecvt(digitalRead(DHTPIN))==laststate){
			counter++;
			delayMicroseconds(1);
			if(counter==255){
				break;
			}
		}
		laststate=sizecvt(digitalRead(DHTPIN));
		if(counter==255) break;

		if((i>=4) && (i%2==0)){
			dht22_dat[j/8] <<= 1;
			if(counter > 50)
				dht22_dat[j/8] |= 1;
			j++;
		}
	}
	if((j>=40) && (dht22_dat[4]==((dht22_dat[0]+dht22_dat[1]+dht22_dat[2]+dht22_dat[3]) &0xFF)) ){
		float t;
		t=(float)(dht22_dat[2] & 0x7F) * 256 + (float)dht22_dat[3];
		t/=10.0;
		if((dht22_dat[2]& 0x80)!=0) t*= -1;
		ret_temp = (int)t;
		return ret_temp;
	}
	else{
		printf("Data not good, skip\n");
		return 0;
	}
}
int read_mcp3208_adc(unsigned char adcChannel){
	unsigned char buff[3];
	int adcValue=0;

	buff[0]=0x06 | ((adcChannel & 0x07)>>2);
	buff[1]=((adcChannel&0x07)<<6);
	buff[2]=0x00;

	digitalWrite(CS_MCP3208,0);
	wiringPiSPIDataRW(SPI_CHANNEL,buff,3);
	buff[1]=0x0f&buff[1];
	adcValue=(buff[1]<<8) | buff[2];

	digitalWrite(CS_MCP3208,1);
	return adcValue;
}
void put(int value){
	buffer[fill_ptr]=value;
	fill_ptr=(fill_ptr+1)%MAX;
	count++;
}
int gettemp(){

	int tmp= buffer[temp_cur];
	temp_cur=(temp_cur+1)%MAX;
	
	return tmp;
}

int get(){
	int tmp=buffer[use_ptr];
	use_ptr=(use_ptr+1)%MAX;
	count--;
	return tmp;
}
void put1(int value){
	buffer1[fill_ptr1]=value;
	fill_ptr1=(fill_ptr1+1)%MAX;
	count1++;
}
int get1(){
	int tmp=buffer1[use_ptr1];
	use_ptr1=(use_ptr1+1)%MAX;
	count1--;
	return tmp;
}
void *tempthread(void *arg){
	int i;
	int received_temp=0;
	unsigned char adcChannel_light=0;
	int adcValue_light=0;
	digitalWrite(FAN,0);	
	digitalWrite(RGBLEDPOWER,0);
	while(1){
		pthread_mutex_lock(&mutex);

		while(count == MAX && count1==MAX)
			pthread_cond_wait(&empty, &mutex);
		while(read_dht22_dat()==0){
			delay(500);
		}
		received_temp=ret_temp;
		put(received_temp);
		printf("temperature : %d\n",received_temp);
		if(received_temp<=25){
			fanflag=1;
			pthread_cond_signal(&fill1);
			pthread_mutex_unlock(&mutex);
			sleep(0.05);
			pthread_mutex_lock(&mutex);
			fanflag=0;
		}
		
		pinMode(CS_MCP3208,OUTPUT);
		adcValue_light=read_mcp3208_adc(adcChannel_light);
		put1(adcValue_light);
		printf("light_sensor : %d\n",adcValue_light);
		if(adcValue_light>=800){
			pthread_cond_signal(&fill2);
			pthread_mutex_unlock(&mutex);
			sleep(0.1);
			pthread_mutex_lock(&mutex);
		}
		
		//while(count==MAX)
		//	pthread_cond_wait(&empty,&mutex);
		//put(received_temp);
		pthread_cond_signal(&fill);
		
		pthread_mutex_unlock(&mutex);
	}
}

void *fanthread(void *arg){
	while(1){
		pthread_mutex_lock(&mutex);
		while(count == 0)
			pthread_cond_wait(&fill1,&mutex);
		while(fanflag==0)
			pthread_cond_wait(&fill1,&mutex);
		int temp = gettemp();
		if(temp <= 25){	
			digitalWrite(FAN, 1);
			printf("fanon!\n");
		//delay(5000);
		//digitalWrite(FAN,0);
		//printf("fanoff!\n");
		}
		//delay(1000);	
		delay(5000);
		pthread_mutex_unlock(&mutex);
		if(temp<=25){
			digitalWrite(FAN,0);
			printf("fanoff!\n");
			delay(1000);
		}
	}
}
void *ledthread(void *arg){
	while(1){
		pthread_mutex_lock(&mutex);
		
		while(count1==0)
			pthread_cond_wait(&fill2,&mutex);
		int temp;

		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);
	}
}
void *dbthread(void *arg){
	int i;
	char query[255];
	while(1){
		pthread_mutex_lock(&mutex);
		while(count == 0)
			pthread_cond_wait(&fill,&mutex);
		int tmp=get();
		int tmp1=get1();
		printf("get_temp : %d\n",tmp);
		printf("get_light : %d\n",tmp1);
		sprintf(query,"insert into thl values(now(),%d,%d,%d)",tmp,0,tmp1);
		if(mysql_query(connector,query)){
			fprintf(stderr,"%s\n",mysql_error(connector));
			printf("Write DB error\n");
		}
		delay(5000);
		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);
		
	}
}

int main(int argc,char *argv[]){
	pthread_t p1,p2,p3,p4;
	if(wiringPiSetup()==-1)
		exit(EXIT_FAILURE);
	if(setuid(getuid())<0){
		perror("Dropping privileges failed\n");
		exit(EXIT_FAILURE);
	}
	Bpluspinmodeset();
	connector = mysql_init(NULL);
	if(!mysql_real_connect(connector,DBHOST,DBUSER,DBPASS,DBNAME,3306,NULL,0)){
		fprintf(stderr,"%s\n",mysql_error(connector));
		return 0;
	}
	
	pthread_create(&p1,NULL,tempthread,NULL);
	pthread_create(&p2,NULL,dbthread,NULL);
	pthread_create(&p3,NULL,fanthread,NULL);
	pthread_create(&p4,NULL,ledthread,NULL);

	pthread_join(p1,NULL);
	pthread_join(p3,NULL);
	pthread_join(p4,NULL);
	pthread_join(p2,NULL);
	
	return 0;
}
