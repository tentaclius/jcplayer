#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <libguile.h>

//TODO: proper makefile with pkg-config
//TODO: incorporate fftw library for good DFT support

#define JC_CLIENT_NAME "J-C-Scheme"
#define JC_FILE_NAME "script.scm"
#define JC_FUNC_NAME "f"
#define JC_RINGBUFFER_SIZE 10000
#define JC_TMP_BUFFER_SIZE 1024

#define RC_OK 0
#define RC_FAIL 1

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef jack_default_audio_sample_t sample_t;

void display_error(const char *fmt, ...)
{
   va_list argP;
   va_start(argP, fmt);

   fprintf(stderr, "ERROR: ");
   vfprintf(stderr, fmt, argP);
   fprintf(stderr, "\n");

   va_end(argP);
}

void display_info(const char *fmt, ...)
{
   va_list argP;
   va_start(argP, fmt);

   fprintf(stdout, "INFO: ");
   vfprintf(stdout, fmt, argP);
   fprintf(stdout, "\n");

   va_end(argP);
}

typedef struct
{
   jack_client_t *client;
   jack_port_t *input_port;
   jack_port_t *output_port;
   jack_ringbuffer_t *ringbuffer;
} jack_setup_t;

jack_setup_t *g_sh_jack;
jack_nframes_t g_sample_rate;

void jack_shutdown_cb(void *arg)
{
   display_error("JACK server has been stopped");
   exit(2);
}

int jack_process_cb(jack_nframes_t nframes, void *arg)
{
   jack_setup_t *setup = (jack_setup_t*) arg;

   jack_default_audio_sample_t *out_buffer = (jack_default_audio_sample_t *) jack_port_get_buffer(setup->output_port, nframes);
   if (out_buffer == NULL) {
      display_error("Can not obtain output buffer");
      return RC_FAIL;
   }

   char *out_p = (char*) out_buffer;
   size_t read_len = nframes * sizeof(sample_t); 
   while (read_len > 0) {
      size_t l = jack_ringbuffer_read(setup->ringbuffer, out_p, read_len);
      read_len -= l;
      out_p += l;
   }

   return RC_OK;
}

int jack_samplerate_cb(jack_nframes_t nframes, void *arg)
{
   g_sample_rate = nframes;
   return 0;
}

int init_jack(jack_setup_t *setup)
{
   g_sh_jack = setup;

   jack_options_t options = JackNullOption;
   jack_status_t status;

   if ((setup->client = jack_client_open(JC_CLIENT_NAME, options, &status)) == NULL) {
      display_error("Cannot create JACK client");
      return RC_FAIL;
   }

   if ((setup->ringbuffer = jack_ringbuffer_create(JC_RINGBUFFER_SIZE)) == NULL) {
      display_error("Can not create JACK ringbuffer");
      return RC_FAIL;
   }

   if (jack_set_process_callback(setup->client, jack_process_cb, setup) != 0) {
      display_error("Cannot set JACK process callback");
      return RC_FAIL;
   }

   if (jack_set_sample_rate_callback(setup->client, jack_samplerate_cb, setup) != 0) {
      display_error("Can not set JACK sample rate callback");
      return RC_FAIL;
   }

   jack_on_shutdown(setup->client, jack_shutdown_cb, setup);

   setup->input_port = jack_port_register(setup->client, "input",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput,  0);
   if (setup->input_port == NULL) {
      display_error("Can not register an input port");
      return RC_FAIL;
   }

   setup->output_port = jack_port_register(setup->client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
   if (setup->output_port == NULL) {
      display_error("Can not register an output port");
      return RC_FAIL;
   }

   if (jack_activate(setup->client) != 0) {
      display_error("Can not activate JACK client");
      return RC_FAIL;
   }

   display_info("JACK client created");
   return RC_OK;
}

void shutdown_jack(jack_setup_t *setup)
{
   g_sh_jack = NULL;
   jack_port_unregister(setup->client, setup->input_port);
   jack_port_unregister(setup->client, setup->output_port);
   jack_client_close(setup->client);
   display_info("JACK client terminated");
}

typedef struct
{
   xcb_connection_t *connection;
   xcb_window_t root_window;
   uint16_t root_width;
   uint16_t root_height;
   int16_t root_x;
   int16_t root_y;
} my_xcb_setup_t;
my_xcb_setup_t *g_xcb;

int init_xcb(my_xcb_setup_t *setup)
{
   int rc = RC_OK;

   assert(setup != NULL);
   setup->connection = xcb_connect(NULL, NULL);
   assert(setup->connection != NULL);

   if ((rc = xcb_connection_has_error(setup->connection)) != 0) {
      display_error("XCB connection can not be established; error code %d", rc);
      xcb_disconnect(setup->connection);
      return RC_FAIL;
   }

   {
   const xcb_setup_t *xsetup = xcb_get_setup(setup->connection);
   assert(xsetup != NULL);
   xcb_screen_iterator_t screen_it = xcb_setup_roots_iterator(xsetup);
   assert(screen_it.data != NULL);
   xcb_screen_t *screen = screen_it.data;
   setup->root_window = screen->root;
   }

   {
   xcb_generic_error_t *e = NULL;
   xcb_get_geometry_cookie_t cookie = xcb_get_geometry(setup->connection, setup->root_window);
   xcb_get_geometry_reply_t *reply_p = xcb_get_geometry_reply(setup->connection, cookie, &e);

   setup->root_x = reply_p->x;
   setup->root_y = reply_p->y;
   setup->root_width = reply_p->width;
   setup->root_height = reply_p->height;

   if (reply_p) free(reply_p);
   if (e) { free(e); rc = RC_FAIL; }
   }

   g_xcb = setup;
   return rc;
}

void shutdown_xcb(my_xcb_setup_t *setup)
{
   g_xcb = NULL;
   xcb_disconnect(setup->connection);
}

double xcb_mouse_x(my_xcb_setup_t *setup)
{
   double x = 0;
   xcb_generic_error_t *e = NULL;

   xcb_query_pointer_cookie_t cookie = xcb_query_pointer(setup->connection, setup->root_window);
   xcb_query_pointer_reply_t *reply_p = xcb_query_pointer_reply(setup->connection, cookie, &e);

   if (e == NULL)
      x = ((double)reply_p->root_x) / setup->root_width;

   if (reply_p) free(reply_p);
   if (e) free(e);

   return x;
}

SCM scm_mouse_x()
{
   return scm_from_double(g_xcb ? xcb_mouse_x(g_xcb) : 0.0);
}

double xcb_mouse_y(my_xcb_setup_t *setup)
{
   double y = 0;
   xcb_generic_error_t *e = NULL;

   xcb_query_pointer_cookie_t cookie = xcb_query_pointer(setup->connection, setup->root_window);
   xcb_query_pointer_reply_t *reply_p = xcb_query_pointer_reply(setup->connection, cookie, &e);

   if (e == NULL)
      y = ((double)reply_p->root_y) / setup->root_height;

   if (reply_p) free(reply_p);
   if (e) free(e);

   return y;
}

SCM scm_mouse_y()
{
   return scm_from_double(g_xcb ? xcb_mouse_y(g_xcb) : 0.0);
}

typedef struct
{
   pthread_t worker_thread;
   jack_ringbuffer_t *buffer;
   int shutdown_flag;
   int reload_flag;
} guile_setup_t;

typedef struct {
   SCM func;
   long long unsigned t;
} generator_arg_t;

SCM guile_load_file(void *arg)
{
   scm_c_primitive_load(JC_FILE_NAME);
   return scm_c_eval_string(JC_FUNC_NAME);
}

SCM guile_run_generator(void *arg)
{
   generator_arg_t *generator_args = (generator_arg_t*) arg;

   SCM v = scm_call_1(generator_args->func, scm_from_int64(generator_args->t));
   if (!scm_is_number(v)) {
      display_error("Incorrect return type from the generator function");
      return SCM_EOL;
   }

   return v;
}

SCM guile_exception_handler(void *handler_data, SCM key, SCM parameters)
{
   char *key_str = NULL;
   key_str = scm_to_locale_string(scm_symbol_to_string(key));
   display_error("Guile exception caught of type %s", key_str);
   free(key_str);

   return SCM_EOL;
}

SCM scm_sample_rate()
{
   return scm_from_int64((int64_t)g_sample_rate);
}

void* guile_thread_func(void *arg)
{
   guile_setup_t *setup = (guile_setup_t*) arg;
   sample_t data[JC_TMP_BUFFER_SIZE];
   generator_arg_t generator_args;

   scm_init_guile();
   scm_c_define_gsubr("sample-rate", 0, 0, 0, scm_sample_rate);
   scm_c_define_gsubr("mouse-x", 0, 0, 0, scm_mouse_x);
   scm_c_define_gsubr("mouse-y", 0, 0, 0, scm_mouse_y);

   while (!setup->shutdown_flag) {
      generator_args.func = scm_c_catch(SCM_BOOL_T, guile_load_file, NULL, guile_exception_handler, NULL, NULL, NULL);
      if (scm_is_false(generator_args.func)) {
         display_error("Can not load Guile script");
         setup->shutdown_flag = 1;

         kill(getpid(), SIGCHLD);
         return NULL;
      }

      setup->reload_flag = 0;
      display_info("Guile generator function loaded");

      while (!setup->shutdown_flag && !setup->reload_flag)
      {
         size_t avail;
         while ((avail = MIN(jack_ringbuffer_write_space(setup->buffer) / sizeof(sample_t), JC_TMP_BUFFER_SIZE)) == 0);

         for (unsigned i = 0; i < avail; i ++) {
            SCM v = scm_c_catch(SCM_BOOL_T, guile_run_generator, &generator_args, guile_exception_handler, NULL, NULL, NULL);
            if (scm_is_null(v)) {
               display_error("Generator function execution resulted in an error");
               kill(getpid(), SIGCHLD);
               return NULL;
            }
            data[i] = (sample_t) scm_to_double(v);
            generator_args.t ++;
         }

         jack_ringbuffer_write(setup->buffer, (const char*) data, avail * sizeof(sample_t));
      }
   }

   return NULL;
}

int init_guile_thread(guile_setup_t *setup, jack_ringbuffer_t *buf)
{
   setup->shutdown_flag = 0;
   setup->reload_flag = 0;
   setup->buffer = buf;

   if (pthread_create(&setup->worker_thread, NULL, guile_thread_func, (void*) setup) != 0) {
      display_error("Can not create Guile worker thread");
      return RC_FAIL;
   }

   return RC_OK;
}

void shutdown_guile_thread(guile_setup_t *setup)
{
   setup->shutdown_flag = 1;
   pthread_join(setup->worker_thread, NULL);
   display_info("Guile thread terminated");
}

void guile_reload_func(guile_setup_t *setup)
{
   setup->reload_flag = 1;
}

void thread_term_handler(int s)
{
   display_error("Guile thread terminated");
   if (g_sh_jack != NULL)
      shutdown_jack(g_sh_jack);
   exit(1);
}

int main(int argc, char **argv)
{
   jack_setup_t jack_setup;
   guile_setup_t guile_setup;
   my_xcb_setup_t xcb_setup;

   if (init_jack(&jack_setup) != RC_OK) {
      display_error("JACK initialization failed. The program exits");
      exit(1);
   }

   struct sigaction action;
   sigemptyset(&action.sa_mask);
   action.sa_handler = thread_term_handler;
   action.sa_flags = 0;

   if (sigaction(SIGCHLD, &action, NULL) != 0) {
      perror("sigaction");
      exit(1);
   }

   if (init_guile_thread(&guile_setup, jack_setup.ringbuffer) != RC_OK) {
      display_error("Guile initialization failed. The program exits");
      shutdown_jack(&jack_setup);
      exit(1);
   }

   if (init_xcb(&xcb_setup) != RC_OK) {
      display_error("Can not initialize XCB");
      exit(1);
   }

   while (getchar() != EOF) {
      guile_reload_func(&guile_setup);
   }

   shutdown_guile_thread(&guile_setup);
   shutdown_jack(&jack_setup);
   shutdown_xcb(&xcb_setup);

   exit(0);
}
