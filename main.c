#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t keep_running = 1;
int serial_fd = -1;
FILE *log_file = NULL;

void handle_signal(int sig) {
  if (sig == SIGINT || sig == SIGTERM)
    keep_running = 0;
}

void get_startup_timestamp(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buffer, size, "%Y%m%d_%H%M%S.log", t);
}

void get_log_timestamp(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buffer, size, "[%Y-%m-%d %H:%M:%S]", t);
}

int setup_uart(const char *device) {
  int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0)
    return -1;
  struct termios options;
  tcgetattr(fd, &options);
  cfsetispeed(&options, B115200);
  cfsetospeed(&options, B115200);
  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_iflag &= ~(IXON | IXOFF | IXANY);
  options.c_oflag &= ~OPOST;
  options.c_cc[VMIN] = 1;
  options.c_cc[VTIME] = 0;
  tcsetattr(fd, TCSANOW, &options);
  return fd;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Використання: %s <інтерфейс>\n", argv[0]);
    return EXIT_FAILURE;
  }

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  char log_name[64];
  get_startup_timestamp(log_name, sizeof(log_name));
  log_file = fopen(log_name, "w");
  if (!log_file)
    return EXIT_FAILURE;

  serial_fd = setup_uart(argv[1]);
  if (serial_fd < 0) {
    fprintf(stderr, "Помилка: відсутній або немає доступу до %s\n", argv[1]);
    fprintf(log_file, "Помилка: відсутній або немає доступу до %s\n", argv[1]);
    fclose(log_file);
    return EXIT_FAILURE;
  }

  fprintf(log_file, "--- Сервер запущено на %s ---\n", argv[1]);
  fflush(log_file);

  char buf[256];
  int pos = 0;
  char c;

  while (keep_running) {
    int n = read(serial_fd, &c, 1);
    if (n > 0) {
      buf[pos++] = c;
      if (pos >= (int)sizeof(buf)) {
        pos = 0;
        continue;
      }

      if (pos >= 2 && buf[pos - 2] == '\r' && buf[pos - 1] == '\n') {
        buf[pos] = '\0';

        char log_ts[32];
        get_log_timestamp(log_ts, sizeof(log_ts));

        char log_msg[256];
        strncpy(log_msg, buf, pos - 2);
        log_msg[pos - 2] = '\0';
        fprintf(log_file, "%s Отримано: %s\n", log_ts, log_msg);
        fflush(log_file);

        for (int i = 0; i < pos - 2; i++) {
          if (islower(buf[i]))
            buf[i] = toupper(buf[i]);
          else if (isupper(buf[i]))
            buf[i] = tolower(buf[i]);
        }

        write(serial_fd, buf, pos);
        pos = 0;
      }
    } else if (n < 0 && errno != EINTR) {
      break;
    }
  }

  if (serial_fd >= 0)
    close(serial_fd);
  if (log_file) {
    fprintf(log_file, "--- Сервер зупинено ---\n");
    fclose(log_file);
  }
  return EXIT_SUCCESS;
}
