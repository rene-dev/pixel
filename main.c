#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 2048

#define XSTR(a) #a
#define STR(a) XSTR(a)

uint8_t* pixels;
volatile int running = 1;
volatile int client_thread_count = 0;
volatile int server_sock;

void * handle_client(void *);
void * handle_clients(void *);

static inline void set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
		if(a == 255){ // fast & usual path
			pixels[(y * PIXEL_WIDTH + x) * 4 + 0] = r;
			pixels[(y * PIXEL_WIDTH + x) * 4 + 1] = g;
			pixels[(y * PIXEL_WIDTH + x) * 4 + 2] = b;
			return;
		}
		// alpha path
		uint8_t dst_r = pixels[(y * PIXEL_WIDTH + x) * 4 + 0];
		uint8_t dst_g = pixels[(y * PIXEL_WIDTH + x) * 4 + 1];
		uint8_t dst_b = pixels[(y * PIXEL_WIDTH + x) * 4 + 2];
		uint8_t na = 255 - a;
		uint16_t br = r * a + dst_r * na;
		uint16_t bg = g * a + dst_g * na;
		uint16_t bb = b * a + dst_b * na;
		pixels[(y * PIXEL_WIDTH + x) * 4 + 0] = br >> 8;
		pixels[(y * PIXEL_WIDTH + x) * 4 + 1] = bg >> 8;
		pixels[(y * PIXEL_WIDTH + x) * 4 + 2] = bb >> 8;
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
         int i;
         for (i = 0; i < read_pos; i++){
            if (buf[i] == '\n'){
               buf[i] = 0;
#if 1 // mit alpha, aber ggf. instabil
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
                           set_pixel(x, y, c >> 16, c >> 8, c, a);
                        }
                     }
                  }
               }
#else // ohne alpha
               if(sscanf(buf,"PX %u %u %x",&x,&y,&c) == 3){
                  set_pixel(x, y, c >> 16, c >> 8, c, 0xff);
               }
#endif
               else if(!strncmp(buf, "SIZE", 4)){
                  static const char out[] = "SIZE " STR(PIXEL_WIDTH) " " STR(PIXEL_HEIGHT) "\n";
                  send(sock, out, sizeof(out), MSG_DONTWAIT | MSG_NOSIGNAL);
               }
               else{
                  printf("QUATSCH[%i]: ", i);
                  int j;
                  for (j = 0; j < i; j++)
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
   int client_sock;
   socklen_t addr_len;
   struct sockaddr_in addr;
   addr_len = sizeof(addr);
   struct timeval tv;
   
   printf("Starting Server...\n");
   
   server_sock = socket(PF_INET, SOCK_STREAM, 0);

   tv.tv_sec = 2;
   tv.tv_usec = 0;

   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(PORT);
   addr.sin_family = AF_INET;
   
   if (server_sock == -1){
      perror("socket() failed");
      return 0;
   }
   
   if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
      printf("setsockopt(SO_REUSEADDR) failed\n");
   //if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0)
   //   printf("setsockopt(SO_REUSEPORT) failed\n");

   int retries;
   for (retries = 0; bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 && retries < 10; retries++){
      perror("bind() failed ...retry in 5s");
      usleep(5000000);
   }
   if (retries == 10)
      return 0;

   if (listen(server_sock, 3) == -1){
      perror("listen() failed");
      return 0;
   }
   printf("Listening...\n");
   
   setsockopt(server_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv,sizeof(struct timeval));
   setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

   while(running){
      client_sock = accept(server_sock, (struct sockaddr*)&addr, &addr_len);
      if(client_sock > 0){
         printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
         if( pthread_create( &thread_id , NULL ,  handle_client , (void*) &client_sock) < 0)
         {
            close(client_sock);
            perror("could not create thread");
         }
      }
   }
   close(server_sock);
   return 0;
}

static EGLDisplay display;
static EGLSurface surface;
static EGLConfig config;
static EGLContext context;
static DISPMANX_DISPLAY_HANDLE_T dispman_display;
static DISPMANX_UPDATE_HANDLE_T dispman_update;
static DISPMANX_ELEMENT_HANDLE_T dispman_element;
static EGL_DISPMANX_WINDOW_T nativewindow;
static VC_RECT_T dst_rect;
static VC_RECT_T src_rect;
static void LoadOpenGLESWindow(uint32_t width, uint32_t height)
{
    bcm_host_init();
    
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);
    
    static const EGLint attribute_list[] =
	{
	    EGL_RED_SIZE, 8,
	    EGL_GREEN_SIZE, 8,
	    EGL_BLUE_SIZE, 8,
	    EGL_ALPHA_SIZE, 8,
	    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_NONE
	};
    EGLint num_config;
    eglChooseConfig(display, attribute_list, &config, 1, &num_config);
    
    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = width;
    dst_rect.height = height;
    
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = width << 16;
    src_rect.height = height << 16;

    dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update = vc_dispmanx_update_start( 0 );
    dispman_element = vc_dispmanx_element_add(
    	dispman_update, dispman_display,
		0/*layer*/, &dst_rect, 0/*src*/,
		&src_rect, DISPMANX_PROTECTION_NONE, 
		0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
    nativewindow.element = dispman_element;
    nativewindow.width = width;
    nativewindow.height = height;
    vc_dispmanx_update_submit_sync(dispman_update);

    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    eglMakeCurrent(display, surface, surface, context);
}

static GLuint LoadShader(GLenum type, const char *shaderSrc)
{
    GLuint shader = glCreateShader(type);
    if(shader == 0) return 0;
    glShaderSource(shader, 1, &shaderSrc, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if(!compiled)
    {
	    GLint infoLen = 0;
	    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
	    if(infoLen > 1)
		{
		    char* infoLog = malloc(sizeof(char) * infoLen);
		    glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
		    fprintf(stderr, "Error compiling shader:\n%s\n", infoLog);
		    free(infoLog);
		}
	    glDeleteShader(shader);
	    return 0;
	}
    return shader;
}

static GLuint LoadProgram(const char *vp, const char *fp)
{
    GLuint vertexShader = LoadShader(GL_VERTEX_SHADER, vp);
    GLuint fragmentShader = LoadShader(GL_FRAGMENT_SHADER, fp);
    GLuint programObject = glCreateProgram();
    if(programObject == 0) return 0;
    glAttachShader(programObject, vertexShader);
    glAttachShader(programObject, fragmentShader);
    glBindAttribLocation(programObject, 0, "vPosition");
    glBindAttribLocation(programObject, 1, "vTexcoord");
    glLinkProgram(programObject);
    GLint linked;
    glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
    if(!linked)
	{
	    GLint infoLen = 0;
	    glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &infoLen);
	    if(infoLen > 1)
		{
		    char* infoLog = malloc(sizeof(char) * infoLen);
		    glGetProgramInfoLog(programObject, infoLen, NULL, infoLog);
		    fprintf(stderr, "Error linking program:\n%s\n", infoLog);
		    free(infoLog);
		}
	    glDeleteProgram(programObject);
	    return 0;
	}
	return programObject;
}

static const GLfloat quad_vertices[] =
{
	-1.0f,  1.0f,
	 1.0f,  1.0f,
	-1.0f, -1.0f,
	 1.0f, -1.0f,
	-1.0f, -1.0f,
	 1.0f,  1.0f
};

static const GLfloat quad_texcoord[] =
{
	0.0f, 0.0f,
	1.0f, 0.0f,
	0.0f, 1.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f
};

static const char vertex_shader[] =
	"attribute vec2 vPosition;\n"
	"attribute vec2 vTexcoord;\n"
	"varying vec2 uv;\n"
	"void main()\n"
	"{\n"
	    "gl_Position = vec4(vPosition, 0, 1);\n"
	    "uv = vTexcoord;\n"
	"}\n";
static const char fragment_shader[] =
	"precision mediump float;\n"
	"varying vec2 uv;\n"
	"uniform sampler2D tex;\n"
	"void main()\n"
	"{\n"
	    "gl_FragColor = vec4(texture2D(tex, uv).rgb, 1.0); \n"
	"}\n";

int kbhit(void)
{
	struct termios oldt, newt;
	int ch;
	int oldf;

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	ch = getchar();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);

	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}

	return 0;
}
	
int main(int argc, char *argv[])
{
	uint32_t width = 1920, height = 1080;
	LoadOpenGLESWindow(width, height);
    
	// initial opengl state
	glViewport(0, 0, width, height);
	//glEnable(GL_TEXTURE_2D);
	//glDisable(GL_CULL_FACE);
	//glDisable(GL_DEPTH_TEST);
	//glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PIXEL_WIDTH, PIXEL_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	// load program
	GLuint programObject = LoadProgram(vertex_shader, fragment_shader);
	glUseProgram(programObject);
	glUniform1i(glGetUniformLocation(programObject, "tex"), 0);
	
	// load quad data
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad_vertices);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoord);
	glEnableVertexAttribArray(1);

	pixels = calloc(PIXEL_WIDTH * PIXEL_HEIGHT * 4, 1);

	pthread_t thread_id;
	if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0)
	{
		perror("could not create thread");
		free(pixels);
		return 1;
	}
	
	while(!kbhit())
    {
		//glClear(GL_COLOR_BUFFER_BIT);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PIXEL_WIDTH, PIXEL_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		eglSwapBuffers(display, surface);
    }

	running = 0;
	printf("Shutting Down...\n");
	while (client_thread_count)
		usleep(100000);
	close(server_sock);
	pthread_join(thread_id, NULL);
	free(pixels);
	return 0;
}
