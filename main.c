#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


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

  int serial_fd = setup_uart(argv[1]);
  if (serial_fd < 0) {
    fprintf(stderr, "Помилка: неможливо відкрити порт %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  printf("Порт %s успішно відкрито та налаштовано (115200 8N1).\n", argv[1]);

  close(serial_fd);
  return EXIT_SUCCESS;
}
