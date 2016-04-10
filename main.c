#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <SDL.h>

#define PIXEL_WIDTH 800
#define PIXEL_HEIGHT 600
#define PORT 1234

#define BUFSIZE 2048

#define XSTR(a) #a
#define STR(a) XSTR(a)

uint32_t* pixels;
volatile int running = 1;
volatile int client_thread_count = 0;

void * handle_client(void *);
void * handle_clients(void *);

void set_pixel(uint16_t x, uint16_t y, uint32_t c, uint8_t a)
{
   if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
      //TODO: Raspberry pi uses RGB565 -> pixels needs to be uint16_t
      if(a == 255){ // fast & usual path
         pixels[y * PIXEL_WIDTH + x] = 0xff000000 | c; // ARGB
         return;
      }
      // alpha path
      uint8_t src_r = (c >> 16);
      uint8_t src_g = (c >> 8) & 0xff;
      uint8_t src_b = (c & 0xff);
      uint32_t dst_c = pixels[y * PIXEL_WIDTH + x];
      uint8_t dst_r = (dst_c >> 16);
      uint8_t dst_g = (dst_c >> 8) & 0xff;
      uint8_t dst_b = (dst_c & 0xff);
      uint8_t na = 255 - a;
      uint16_t r = src_r * a + dst_r * na;
      uint16_t g = src_g * a + dst_g * na;
      uint16_t b = src_b * a + dst_b * na;
      pixels[y * PIXEL_WIDTH + x] = 0xff000000 | ((r & 0xff00) << 8) | (g & 0xff00) | (b >> 8); // ARGB
   }
}

void * handle_client(void *s){
   client_thread_count++;
   int sock = *(int*)s;
   char buf[BUFSIZE];
   int read_size, read_pos = 0;
   uint32_t x,y,c;
   while(running && (read_size = recv(sock , buf + read_pos, sizeof(buf) - read_pos , 0)) > 0){
      read_pos += read_size;
      int found = 1;
      while (found){
         found = 0;
         for (int i = 0; i < read_pos; i++){
            if (buf[i] == '\n'){
               buf[i] = 0;
#if 0 // mit alpha, aber ggf. instabil
               if(!strncmp(buf, "PX ", 3)){ // ...frag nicht :D...
                  char *pos1 = buf + 3;
                  x = strtoul(buf + 3, &pos1, 10);
                  if(buf != pos1){
                     pos1++;
                     char *pos2 = pos1;
                     y = strtoul(pos1, &pos2, 10);
                     if(pos1 != pos2){
                        pos2++;
                        pos1 = pos2;
                        c = strtoul(pos2, &pos1, 16);
                        if(pos2 != pos1){
                           uint8_t a = 255;
                           if((pos1 - pos2) > 6){ // contains alpha
                              a = c & 0xff;
                              c >>= 8;
                           }
                           set_pixel(x,y,c,a);
                        }
                     }
                  }
               }
#else // ohne alpha
               if(sscanf(buf,"PX %u %u %x",&x,&y,&c) == 3){
                  set_pixel(x,y,c, 0xff);
               }
#endif
               else if(!strncmp(buf, "SIZE", 4)){
                  static const char out[] = "SIZE " STR(PIXEL_WIDTH) " " STR(PIXEL_HEIGHT) "\n";
                  send(sock, out, sizeof(out), MSG_DONTWAIT | MSG_NOSIGNAL);
               }
               else{
                  printf("QUATSCH[%i]: ", i);
                  for (int j = 0; j < i; j++)
                     printf("%c", buf[j]);
                  printf("\n");
               }
               int offset = i + 1;
               int count = read_pos - offset;
               if (count > 0)
                  memmove(buf, buf + offset, count); // TODO: ring buffer?
               read_pos -= offset;
               found = 1;
               break;
            }
         }
         if (sizeof(buf) - read_pos == 0){ // received only garbage for a whole buffer. start over!
            buf[sizeof(buf) - 1] = 0;
            printf("GARBAGE BUFFER: %s\n", buf);
            read_pos = 0;
         }
      }
   }
   close(sock);
   printf("Client disconnected\n");
   fflush(stdout);
   client_thread_count--;
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

   tv.tv_sec = 2;
   tv.tv_usec = 0;

   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(PORT);
   addr.sin_family = AF_INET;

   setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
   
   if (s == -1){
      perror("socket() failed");
      return 0;
   }

   int retries;
   for (retries = 0; bind(s, (struct sockaddr*)&addr, sizeof(addr)) == -1 && retries < 10; retries++){
      perror("bind() failed ...retry in 5s");
      usleep(5000000);
   }
   if (retries == 10)
      return 0;

   if (listen(s, 3) == -1){
      perror("listen() failed");
      return 0;
   }
   printf("Listening...\n");

   while(running){
      client_sock = accept(s, (struct sockaddr*)&addr, &addr_len);
      if(client_sock > 0){
         printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
         if( pthread_create( &thread_id , NULL ,  handle_client , (void*) &client_sock) < 0)
         {
            close(client_sock);
            perror("could not create thread");
            //return 0;
         }
      }
   }
   close(s);
   return 0;
}

int main(){
   SDL_Init(SDL_INIT_VIDEO);
   SDL_ShowCursor(0);

   SDL_Window* window = SDL_CreateWindow(
      "pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
   SDL_RenderClear(renderer);
   
   SDL_Texture* sdlTexture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      PIXEL_WIDTH, PIXEL_HEIGHT);
   if(!sdlTexture){
      printf("could not create texture");
      SDL_Quit();
      return 1;
   }
   
   pixels = malloc(PIXEL_WIDTH * PIXEL_HEIGHT * 4);

   pthread_t thread_id;
   if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0){
      perror("could not create thread");
      free(pixels);
      SDL_Quit();
      return 1;
   }
   
   while(42){
      SDL_UpdateTexture(sdlTexture, NULL, pixels, PIXEL_WIDTH * sizeof(uint32_t));
      SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
      SDL_RenderPresent(renderer);
      SDL_Event event;
      if(SDL_PollEvent(&event)){
         if(event.type == SDL_QUIT){
            break;
         }
         if(event.type == SDL_KEYDOWN){
            if(event.key.keysym.sym == SDLK_q){
               break;
            }
            if(event.key.keysym.sym == SDLK_f){
               uint32_t flags = SDL_GetWindowFlags(window);
               SDL_SetWindowFullscreen(window,
                  (flags & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
               printf("Toggled Fullscreen\n");
            }
         }
      }
   }

   running = 0;
   printf("Shutting Down...\n");
   SDL_DestroyWindow(window);
   while (client_thread_count)
      usleep(100000);
   pthread_join(thread_id, NULL);
   free(pixels);
   SDL_Quit();
   return 0;
}
