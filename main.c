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

typedef uint32_t colorbuf_t;

colorbuf_t *colorbuffer;
int xsize,ysize;

void * handle_client(void *);
void * handle_clients(void *);

void set_pixel(uint16_t x, uint16_t y, uint32_t c)
{
   if(x < xsize && y < ysize){
      //TODO: Raspberry pi uses RGB565 -> colorbuffer needs to be uint16_t
      //TODO: alpha support
      colorbuffer[y * xsize + x] = 0xff000000 | c; // ARGB
   }
}

void * handle_client(void *s){
   const size_t bufsize = 1024;
   struct ringbuf rx_buf = { .buf = (char[bufsize]) {0}, .bufsize = bufsize};
   int sock = *(int*)s;
   char buf[bufsize];
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

int show_modes(){
   int display_in_use = 0; /* Only using first display */

   int i, display_mode_count;
   SDL_DisplayMode mode;
   Uint32 f;

   SDL_Log("SDL_GetNumVideoDisplays(): %i", SDL_GetNumVideoDisplays());

   display_mode_count = SDL_GetNumDisplayModes(display_in_use);
   if (display_mode_count < 1) {
      SDL_Log("SDL_GetNumDisplayModes failed: %s", SDL_GetError());
      return 1;
   }
   SDL_Log("SDL_GetNumDisplayModes: %i", display_mode_count);

   for (i = 0; i < display_mode_count; ++i) {
      if (SDL_GetDisplayMode(display_in_use, i, &mode) != 0) {
         SDL_Log("SDL_GetDisplayMode failed: %s", SDL_GetError());
         return 1;
      }
      f = mode.format;

      SDL_Log("Mode %i\tbpp %i\t%s\t%i x %i", i,
      SDL_BITSPERPIXEL(f), SDL_GetPixelFormatName(f), mode.w, mode.h);
   }
   return 0;
}

int main(){
   SDL_Window* window;
   SDL_Surface* surface;
   
   SDL_Init(SDL_INIT_VIDEO);
   show_modes();
   window = SDL_CreateWindow("pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   surface = SDL_GetWindowSurface(window);
   colorbuffer = (uint32_t*)surface->pixels;
   
   xsize = PIXEL_WIDTH;
   ysize = PIXEL_HEIGHT;
   memset(colorbuffer, 0x00, (PIXEL_WIDTH * PIXEL_HEIGHT * sizeof(colorbuf_t)));

   pthread_t thread_id;
   
   if( pthread_create( &thread_id , NULL ,  handle_clients , NULL) < 0)
   {
      perror("could not create thread");
      return 1;
   }
   
   while(42){
      SDL_UpdateWindowSurface(window);
      
      surface = SDL_GetWindowSurface(window);
      colorbuffer = (colorbuf_t*)surface->pixels;
      
      SDL_Event event;
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
         if(event.type == SDL_WINDOWEVENT){
            if(event.window.event == SDL_WINDOWEVENT_RESIZED){
               //TODO: clear or resize buffer
               //TODO: crashes when client thread writes to colorbuffer during resize
               xsize = event.window.data1;
               ysize = event.window.data2;
               surface = SDL_GetWindowSurface(window);
               colorbuffer = (uint32_t*)surface->pixels;
               //SDL_Log("Window %d resized to %dx%d",event.window.windowID, event.window.data1,event.window.data2);
            }
         }
      }
   } 
}
