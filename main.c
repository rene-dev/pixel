#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <SDL.h>

#include "ringbuf.h"

#define PIXEL_WIDTH 800
#define PIXEL_HEIGHT 600
#define PORT 1234

#define BUFSIZE 1024

uint32_t* pixels;

void * handle_client(void *);
void * handle_clients(void *);

void set_pixel(uint16_t x, uint16_t y, uint32_t c)
{
   if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
      //TODO: Raspberry pi uses RGB565 -> pixels needs to be uint16_t
      //TODO: alpha support
      pixels[y * PIXEL_WIDTH + x] = 0xff000000 | c; // ARGB
   }
}

void * handle_client(void *s){
   struct ringbuf rx_buf = { .buf = (char[BUFSIZE]) {0}, .bufsize = BUFSIZE};
   int sock = *(int*)s;
   char buf[BUFSIZE];
   char line[100];
   int read_size;
   int x,y,c;
   while( (read_size = recv(sock , buf , sizeof(buf) , 0)) > 0 ){
      rb_write(&rx_buf, buf, read_size);//TODO: fix ringbuf to use memcpy
      while(rb_getline(&rx_buf, line,100)){
         if(sscanf(line,"PX %i %i %x",&x,&y,&c) == 3){
            //TODO: SIZE should return canvas size
            set_pixel(x,y,c);
         }
      }
   }
   close(sock);
   printf("Client disconnected\n");
   fflush(stdout);
   return 0;
}

void * handle_clients(void * foobar){
   pthread_t thread_id;
   int s;
   int client_sock;
   socklen_t addr_len;
   struct sockaddr_in addr;
   addr_len = sizeof(addr);
   struct timeval tv;
   
   printf("Starting Server...\n");
   
   s = socket(PF_INET, SOCK_STREAM, 0);

   tv.tv_sec = 5;
   tv.tv_usec = 0;

   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(PORT);
   addr.sin_family = AF_INET;

   setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
   
   if (s == -1){
      perror("socket() failed");
      return 0;
   }

   if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == -1){
      perror("bind() failed");
      return 0;
   }

   if (listen(s, 3) == -1){
      perror("listen() failed");
      return 0;
   }
   printf("Listening...\n");

   while(42){
      client_sock = accept(s, (struct sockaddr*)&addr, &addr_len);
      if(client_sock > 0){
         printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
         if( pthread_create( &thread_id , NULL ,  handle_client , (void*) &client_sock) < 0)
         {
            perror("could not create thread");
            return 0;
         }
      }
   }
   return 0;
}

int main(){
   SDL_Window* window;
   SDL_Texture* sdlTexture;
   pixels = malloc(PIXEL_WIDTH*PIXEL_HEIGHT*4);//TODO: free pixels
   
   SDL_Init(SDL_INIT_VIDEO);
   SDL_ShowCursor(0);

   window = SDL_CreateWindow("pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
   
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
   SDL_RenderClear(renderer);
   SDL_RenderPresent(renderer);
   
   sdlTexture = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,PIXEL_WIDTH, PIXEL_HEIGHT);
   if(sdlTexture == NULL){
      printf("could not create texture");
      SDL_Quit();
      exit(0);
   }

   SDL_Event event;

   pthread_t thread_id;
   
   if( pthread_create( &thread_id , NULL ,  handle_clients , NULL) < 0)
   {
      perror("could not create thread");
      return 1;
   }
   
   while(42){
      SDL_UpdateTexture(sdlTexture, NULL, pixels, PIXEL_WIDTH * sizeof(uint32_t));
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
      SDL_RenderPresent(renderer);
      
      if(SDL_PollEvent(&event))
      {
         if(event.type == SDL_QUIT)
         {
            SDL_Quit();
            exit(0);
         }
         if(event.type == SDL_KEYDOWN)
         {
            if(event.key.keysym.scancode == 20){//q
               //TODO: close connections
               SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
               SDL_Quit();
               exit(0);
            }
            if(event.key.keysym.scancode == 9){//f
               //TODO: toggle fullscreen
               SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            printf("key: %i\n", event.key.keysym.scancode );
         }
      }
   } 
}
