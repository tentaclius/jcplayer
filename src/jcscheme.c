#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <signal.h>

#include <pthread.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include <libguile.h>

#define JACK_CLIENT_NAME "J-C-Scheme"
#define SCM_FILE_NAME "script.scm"
#define SCM_FUNC_NAME "f"
#define RINGBUFFER_SIZE 10000
#define TMP_BUFFER_SIZE 1024

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
   jack_options_t options = JackNullOption;
   jack_status_t status;

   if ((setup->client = jack_client_open(JACK_CLIENT_NAME, options, &status)) == NULL) {
      display_error("Cannot create JACK client");
      return RC_FAIL;
   }

   if ((setup->ringbuffer = jack_ringbuffer_create(RINGBUFFER_SIZE)) == NULL) {
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
   jack_port_unregister(setup->client, setup->input_port);
   jack_port_unregister(setup->client, setup->output_port);
   jack_client_close(setup->client);
   display_info("JACK client terminated");
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
   scm_c_primitive_load(SCM_FILE_NAME);
   return scm_c_eval_string(SCM_FUNC_NAME);
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
   sample_t data[TMP_BUFFER_SIZE];
   generator_arg_t generator_args;

   scm_init_guile();
   scm_c_define_gsubr("sample-rate", 0, 0, 0, scm_sample_rate);

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
         while ((avail = MIN(jack_ringbuffer_write_space(setup->buffer) / sizeof(sample_t), TMP_BUFFER_SIZE)) == 0);

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
   shutdown_jack(g_sh_jack);
   exit(1);
}

int main(int argc, char **argv)
{
   jack_setup_t jack_setup;
   guile_setup_t guile_setup;

   if (init_jack(&jack_setup) != RC_OK) {
      display_error("JACK initialization failed. The program exits");
      exit(1);
   }

   g_sh_jack = &jack_setup;

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

   while (getchar() != EOF) {
      guile_reload_func(&guile_setup);
   }

   shutdown_guile_thread(&guile_setup);
   shutdown_jack(&jack_setup);

   exit(0);
}
