# Звіт з лабораторної роботи: Розробка ECHO-застосунку для UART інтерфейсу

## Мета роботи
Ознайомлення з апаратною організацією та принципами функціонування послідовного асинхронного інтерфейсу UART в ОС Linux на базі мікрокомп'ютера **Raspberry Pi 5 (16 GB RAM)**. Налаштування та активація апаратного UART-порту на 40-піновому роз'ємі розширення GPIO із врахуванням архітектури чипа введення-виведення RP1. Розробка, крос-компіляція та верифікація C-застосунку для двосторонньої передачі даних (ECHO з інверсією регістру символів) з використанням низькорівневого інтерфейсу `termios.h`, механізму логування з часовими мітками, обробки сигналів завершення (`signal.h`) та налаштування автозапуску через службу `systemd`.

---

## 1. Конфігурація апаратного та програмного середовища

| Параметр | HOST (Комп'ютер розробника) | TARGET (Цільова плата) |
| :--- | :--- | :--- |
| **Пристрій / Платформа** | Ноутбук (Laptop x86_64) | **Raspberry Pi 5 Model B (Rev 1.0)** |
| **Процесор / Архітектура** | Intel Core i5 (x86_64) | **Broadcom BCM2712**, 4x ARM Cortex-A76 @ 2.4 GHz (AArch64) |
| **Контролер I/O (Southbridge)**| Вбудований Intel PCH | **Raspberry Pi RP1-C0** (PCIe I/O Controller) |
| **Оперативна пам'ять (RAM)** | **8 GB DDR4** | **16 GB LPDDR4X-4267 SDRAM** |
| **Операційна система** | Ubuntu 24.04 LTS (Linux 6.8.0) | **Raspberry Pi OS Bookworm 64-bit** (Debian 12) |
| **Ядро Linux** | 6.8.0-45-generic | `6.6.31+rpt-rpi-2712` (з підтримкою 16KB Page Size) |
| **IP-адреса / Користувач** | `172.16.217.1` / `dev` | `172.16.217.116` / `xrov` |
| **Інструменти компіляції** | `aarch64-linux-gnu-gcc` 13.2.0 | `gcc` 12.2.0 (native) |
| **UART інтерфейс** | USB-UART перехідник (CP2102, `/dev/ttyUSB0`) | Вбудований PL011 UART на RP1 (`/dev/serial0` -> `/dev/ttyAMA0`) |

### Системні параметри середовища:
**HOST (Ноутбук Core i5, 8GB RAM):**
```bash
dev@laptop:~$ lscpu | grep "Model name"
Model name:              Intel(R) Core(TM) i5-1135G7 @ 2.40GHz

dev@laptop:~$ free -h
               total        used        free      shared  buff/cache   available
Mem:           7.6Gi       2.2Gi       3.6Gi       230Mi       2.4Gi       5.2Gi
Swap:          2.0Gi          0B       2.0Gi
```

### Системна перевірка параметрів TARGET (Raspberry Pi 5 16GB):
```bash
xrov@raspberrypi:~$ uname -a
Linux raspberrypi 6.6.31+rpt-rpi-2712 #1 SMP PREEMPT Debian 1:6.6.31-1+rpt1 (2024-05-29) aarch64 GNU/Linux

xrov@raspberrypi:~$ cat /proc/cpuinfo | grep -E "Model|Hardware"
Hardware	: BCM2712
Model		: Raspberry Pi 5 Model B Rev 1.0

xrov@raspberrypi:~$ free -h
               total        used        free      shared  buff/cache   available
Mem:            15Gi       390Mi        14Gi        16Mi       1.1Gi        15Gi
Swap:          511Mi          0B       511Mi
```

### Параметри послідовного з'єднання
* **Швидкість (Baudrate):** 115200 бод
* **Формат кадру:** 8N1 (8 біт даних, No Parity (без перевірки парності), 1 стоп-біт)
* **Контроль потоку (Flow Control):** None (вимкнено)

---

## 2. Завдання 1: Налаштування та активація UART інтерфейсу на TARGET

### 2.1. Особливості архітектури UART на Raspberry Pi 5
В архітектурі **Raspberry Pi 5** підсистема введення-виведення була суттєво модернізована:
1. **Виділений 3-піновий роз'єм Debug UART:** Між двома портами micro-HDMI додано окремий конектор JST-SH для відладки ядра (підключений безпосередньо до BCM2712, пристрій `/dev/ttyAMA10`).
2. **40-піновий роз'єм GPIO:** Піни 8 (GPIO 14) та 10 (GPIO 15) маршрутизуються через фірмовий чипсет південного моста **RP1** (Raspberry Pi RP1-C0), підключений до SoC через інтерфейс 4x PCIe 2.0. Контролер UART на роз'ємі GPIO обслуговується драйвером PL011 і прив'язаний до пристрою `/dev/ttyAMA0` (символічне посилання `/dev/serial0`).

**Розпіновка роз'єму GPIO для UART:**
* **Pin 8 (GPIO 14):** `UART0_TXD` (Передавач RP1)
* **Pin 10 (GPIO 15):** `UART0_RXD` (Приймач RP1)
* **Pin 6 (GND):** Сигнальна земля

### 2.2. Програмна активація інтерфейсу в Raspberry Pi OS Bookworm
Для використання UART на 40-піновому роз'ємі для призначених для користувача додатків виконано наступні налаштування:

1. **Активація через Device Tree в `/boot/firmware/config.txt`:**
   У нових версіях завантажувача Raspberry Pi 5 конфігураційний файл розташований у `/boot/firmware/config.txt`. До нього додано такі директиви:
   ```ini
   # Активація контролера UART0 на чипі RP1 для 40-пінового роз'єму
   dtparam=uart0=on
   enable_uart=1
   ```

2. **Відключення консолі ядра в `/boot/firmware/cmdline.txt`:**
   Видалено передачу керування консолі ядра на послідовний порт:
   ```text
   # Видалено фрагмент: console=serial0,115200 (або console=ttyAMA0,115200)
   ```
   Це гарантує, що при завантаженні ядро Linux не відправлятиме в порт налагоджувальні логи і не очікуватиме логіну користувача.

3. **Вимкнення системної служби `serial-getty`:**
   ```bash
   xrov@raspberrypi:~$ sudo systemctl stop serial-getty@ttyAMA0.service
   xrov@raspberrypi:~$ sudo systemctl disable serial-getty@ttyAMA0.service
   ```

4. **Перевірка символічних посилань:**
   ```bash
   xrov@raspberrypi:~$ ls -l /dev/serial*
   lrwxrwxrwx 1 root root 7 Oct 12 10:14 /dev/serial0 -> ttyAMA0
   ```

5. **Надання прав доступу користувачеві `xrov`:**
   Файл пристрою `/dev/ttyAMA0` належить групі `dialout`:
   ```bash
   xrov@raspberrypi:~$ ls -l /dev/ttyAMA0
   crw-rw---- 1 root dialout 204, 64 Oct 12 10:15 /dev/ttyAMA0
   ```
   Додавання користувача до групи `dialout`:
   ```bash
   xrov@raspberrypi:~$ sudo usermod -aG dialout xrov
   xrov@raspberrypi:~$ groups xrov
   xrov : xrov adm dialout cdrom sudo audio video plugdev games users input render netdev
   ```

### 2.3. Loopback-тест
Для перевірки функціонування контролера на чипі RP1 та верифікації прав доступу проведено апаратний loopback-тест:
1. За допомогою перемички фізично з'єднано **Pin 8 (TXD)** та **Pin 10 (RXD)** на 40-піновому роз'ємі Raspberry Pi 5.
2. Запущено термінальну програму `picocom` від імені звичайного користувача `xrov` (без `sudo`):
   ```bash
   xrov@raspberrypi:~$ picocom -b 115200 /dev/serial0
   picocom v3.1

   port is        : /dev/serial0
   flowcontrol    : none
   baudrate is    : 115200
   parity is      : none
   databits are   : 8
   stopbits are   : 1
   escape is      : C-a
   local echo is  : default

   Type [C-a] [C-h] to see available commands
   Terminal ready
   ```
3. **Результати тесту:** 
   - Символи, введені з клавіатури, негайно відображалися в терміналі через електричний зворотний зв'язок з лінії TX в RX.
   - При вилученні перемички відлуння миттєво зникало.
   - Тест підтвердив коректність генерації тактових імпульсів чипом RP1, цілісність ліній вводу/виводу та наявність прав доступу для `xrov`.

---

## 3. Завдання 2: Розробка ECHO-застосунку (`mycom`)

### 3.1. Структура проекту
```text
ajax-uart/
├── .clang-format    # Конфігурація стилю коду (Chromium style, 2 spaces)
├── Makefile         # Сценарій збирання з підтримкою CROSS_COMPILE та deploy
├── main.c           # Вихідний код програми (ECHO, логування, сигнали)
├── mycom.service    # Unit-файл для systemd
└── Report.md        # Звіт з виконання лабораторної роботи
```

### 3.2. Конфігурація порту через `termios.h` (п. 2)
Для роботи з послідовним портом використовується POSIX-сумісний інтерфейс `termios.h`. Функція `setup_uart` ініціалізує порт у «сирому» (raw) режимі:
```c
int setup_uart(const char *device) {
  int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0)
    return -1;
  struct termios options;
  tcgetattr(fd, &options);

  // Встановлення швидкості 115200 бод для вхідного та вихідного потоків
  cfsetispeed(&options, B115200);
  cfsetospeed(&options, B115200);

  // Конфігурація 8N1 (8 біт даних, 1 стоп-біт, без біта паритету)
  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;
  options.c_cflag |= (CLOCAL | CREAD); // Дозвіл прийому, ігнорування ліній модему

  // Сирий режим: вимкнення канонічної буферизації рядків, луни та сигналів керування
  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  // Відключення програмного контролю потоку (XON/XOFF)
  options.c_iflag &= ~(IXON | IXOFF | IXANY);
  // Відключення спеціальної обробки виводу (OPOST)
  options.c_oflag &= ~OPOST;

  // Блокуюче посимвольне читання (VMIN=1, VTIME=0)
  options.c_cc[VMIN] = 1;
  options.c_cc[VTIME] = 0;

  tcsetattr(fd, TCSANOW, &options);
  return fd;
}
```

### 3.3. Реалізація `Makefile` (п. 2.а)
Файл `Makefile` підтримує аргумент `CROSS_COMPILE` для збирання на комп'ютері розробника для архітектури AArch64 (Raspberry Pi 5) та ціль `deploy` для автоматичного копіювання бінарника на плату через `scp`:
```makefile
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mycom
SRC = main.c

DEPLOY_USER ?= xrov
DEPLOY_HOST ?= 172.16.217.116
DEPLOY_PATH ?= /home/$(DEPLOY_USER)/

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.log

deploy: $(TARGET)
	scp $(TARGET) $(DEPLOY_USER)@$(DEPLOY_HOST):$(DEPLOY_PATH)
```

**Процес крос-компіляції на HOST та розгортання:**
```bash
dev@laptop:~/ajax-uart$ make CROSS_COMPILE=aarch64-linux-gnu-
aarch64-linux-gnu-gcc -Wall -Wextra -O2 -o mycom main.c

dev@laptop:~/ajax-uart$ file mycom
mycom: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=f704..., for GNU/Linux 3.7.0, not stripped

dev@laptop:~/ajax-uart$ make deploy
scp mycom xrov@172.16.217.116:/home/xrov/
mycom                                       100%   71KB   8.2MB/s   00:00
```

### 3.4. Параметр командного рядка (п. 2.б)
Програма приймає ім'я пристрою інтерфейсу як перший аргумент (`argv[1]`):
```bash
./mycom /dev/serial0
```
У разі запуску без аргументів виводиться зрозуміла інструкція користувачу у потік `stderr`:
```c
if (argc < 2) {
  fprintf(stderr, "Використання: %s <інтерфейс>\n", argv[0]);
  return EXIT_FAILURE;
}
```

### 3.5. Алгоритм ECHO та інверсії регістру (п. 2.в)
Повідомлення накопичується у буфер до моменту виявлення кінцевої послідовності `\r\n` (символи `CR` `LF`). Після отримання повного рядка виконується посимвольна інверсія:
```c
if (pos >= 2 && buf[pos - 2] == '\r' && buf[pos - 1] == '\n') {
  buf[pos] = '\0';
  ...
  // Інверсія регістру символів повідомлення
  for (int i = 0; i < pos - 2; i++) {
    if (islower(buf[i]))
      buf[i] = toupper(buf[i]);
    else if (isupper(buf[i]))
      buf[i] = tolower(buf[i]);
  }

  // Відправка відповіді назад у UART з символами \r\n
  write(serial_fd, buf, pos);
  pos = 0;
}
```

### 3.6. Логування та обробка помилок (п. 2.г)
1. **Ім'я файлу логу:** Формується при старті застосунку за шаблоном `YYYYMMDD_HHMMSS.log`:
   ```c
   void get_startup_timestamp(char *buffer, size_t size) {
     time_t now = time(NULL);
     struct tm *t = localtime(&now);
     strftime(buffer, size, "%Y%m%d_%H%M%S.log", t);
   }
   ```
2. **Запис отриманих повідомлень:** Кожен успішно прийнятий рядок фіксується у файлі логу із поточною датою та часом:
   ```c
   fprintf(log_file, "%s Отримано: %s\n", log_ts, log_msg);
   fflush(log_file);
   ```
3. **Обробка позаштатних ситуацій:** Якщо порт зайнятий, відсутній або до нього немає доступу, помилка повідомляється на екран у `stderr` та фіксується у файлі логу перед виходом:
   ```c
   serial_fd = setup_uart(argv[1]);
   if (serial_fd < 0) {
     fprintf(stderr, "Помилка: відсутній або немає доступу до %s\n", argv[1]);
     fprintf(log_file, "Помилка: відсутній або немає доступу до %s\n", argv[1]);
     fclose(log_file);
     return EXIT_FAILURE;
   }
   ```

### 3.7. Обробка сигналів завершення (п. 2.д)
Використовується бібліотека `signal.h`. Обробник перехоплює сигнали `SIGINT` (Ctrl+C) та `SIGTERM` (запит на зупинку від ОС чи systemd):
```c
volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
  if (sig == SIGINT || sig == SIGTERM)
    keep_running = 0;
}
```
Після виходу з основного циклу `while (keep_running)` програма акуратно закриває дескриптор `serial_fd`, робить запис `--- Сервер зупинено ---` у файл логу та закриває `log_file`.

---

## 4. Додаткове завдання (п. 2.е*): Налаштування автозапуску через systemd

Для забезпечення роботи застосунку як системного сервісу (демона) створено файл `mycom.service`:

```ini
[Unit]
Description=UART Echo Service (mycom) on Raspberry Pi 5
After=network.target

[Service]
Type=simple
User=xrov
WorkingDirectory=/home/xrov
ExecStart=/home/xrov/mycom /dev/serial0
Restart=on-failure
RestartSec=3s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### Команди встановлення та перевірки на Raspberry Pi 5:
```bash
xrov@raspberrypi:~$ sudo cp mycom.service /etc/systemd/system/
xrov@raspberrypi:~$ sudo systemctl daemon-reload
xrov@raspberrypi:~$ sudo systemctl enable mycom.service
Created symlink /etc/systemd/system/multi-user.target.wants/mycom.service → /etc/systemd/system/mycom.service.

xrov@raspberrypi:~$ sudo systemctl start mycom.service
xrov@raspberrypi:~$ systemctl status mycom.service
● mycom.service - UART Echo Service (mycom) on Raspberry Pi 5
     Loaded: loaded (/etc/systemd/system/mycom.service; enabled; preset: enabled)
     Active: active (running) since Sun 2026-10-11 17:15:30 EEST; 12s ago
   Main PID: 1845 (mycom)
      Tasks: 1 (limit: 16384)
     Memory: 680.0K
        CPU: 5ms
     CGroup: /system.slice/mycom.service
             └─1845 /home/xrov/mycom /dev/serial0
```
*Коментар:* Завдяки 16 GB RAM ліміт завдань системи (`Tasks`) становить 16384, служба споживає мінімум ресурсів (< 1 MB RAM) та успішно автоматично перезапускається після перезавантаження плати (`sudo reboot`).

---

## 5. Результати тестування та верифікація роботи

### 5.1. Схема апаратного стенду (HOST <-> Raspberry Pi 5)
```
+------------------------------------+                   +------------------------------------+
|  HOST (Ubuntu Laptop Core i5, 8GB) |                   |  TARGET (Raspberry Pi 5 16GB)      |
|                                    |                   |                                    |
|  USB-to-UART (CP2102)              |                   |  40-pin GPIO Header (RP1 chipset)  |
|    TXD  ---------------------------+------------------>|  RXD (Pin 10 / GPIO 15)            |
|    RXD  <--------------------------+-------------------|  TXD (Pin 8  / GPIO 14)            |
|    GND  ---------------------------+-------------------|  GND (Pin 6)                       |
|                                    |                   |                                    |
|  /dev/ttyUSB0 (picocom)            |                   |  /dev/serial0 (mycom daemon)       |
+------------------------------------+                   +------------------------------------+
```

### 5.2. Сеанс обміну даними з боку HOST
На HOST-комп'ютері встановлено зв'язок через утиліту `picocom`:
```bash
dev@laptop:~$ picocom -b 115200 --omap crlf /dev/ttyUSB0
picocom v3.1
Terminal ready

Hello World!
hELLO wORLD!

Raspberry Pi 5 16GB - UART Echo Active
rASPBERRY pI 5 16gb - uart eCHO aCTIVE

Ajax Systems Lab 2026
aJAX sYSTEMS lAB 2026
```
*Результат:* Кожне повідомлення, завершене послідовністю `\r\n`, коректно інвертується за регістром (малі літери перетворюються у великі, великі — у малі).

### 5.3. Перевірка згенерованого лог-файлу на TARGET
```bash
xrov@raspberrypi:~$ ls -l *.log
-rw-r--r-- 1 xrov xrov 276 Oct 11 17:16 20261011_171530.log

xrov@raspberrypi:~$ cat 20261011_171530.log
--- Сервер запущено на /dev/serial0 ---
[2026-10-11 17:16:04] Отримано: Hello World!
[2026-10-11 17:16:18] Отримано: Raspberry Pi 5 16GB - UART Echo Active
[2026-10-11 17:16:32] Отримано: Ajax Systems Lab 2026
--- Сервер зупинено ---
```

### 5.4. Тестування обробки помилок
Запуск програми з неіснуючим або заблокованим портом:
```bash
xrov@raspberrypi:~$ ./mycom /dev/ttyNonExistent
Помилка: відсутній або немає доступу до /dev/ttyNonExistent
xrov@raspberrypi:~$ echo $?
1

xrov@raspberrypi:~$ cat 20261011_172210.log
Помилка: відсутній або немає доступу до /dev/ttyNonExistent
```
*Результат:* Програма фіксує помилку одночасно у терміналі (потік `stderr`) та у щойно створеному файлі журналу, після чого повертає ненульовий код помилки `1`.

### 5.5. Тестування штатного завершення за сигналами
При виклику `sudo systemctl stop mycom` (надсилання сигналу `SIGTERM`) або натисканні `Ctrl+C` у консолі:
```bash
xrov@raspberrypi:~$ ./mycom /dev/serial0
^C
xrov@raspberrypi:~$ echo $?
0
```
У файлі логу гарантовано з'являється завершальний запис `--- Сервер зупинено ---`, дескриптор пристрою звільняється в ядрі.

---

## 6. Дотримання Codestyle та історія комітів

### 6.1. Стиль коду (.clang-format)
Стиль коду проекту відповідає єдиному формату на основі стилю Chromium (відступи 2 пробіли, обмеження довжини рядка 80 символів, сортування заголовних файлів):
```bash
dev@laptop:~/ajax-uart$ clang-format --dry-run --Werror main.c
dev@laptop:~/ajax-uart$ echo $?
0
```

### 6.2. Історія комітів у репозиторії Git
Розробка велася ітеративно, з регулярними та логічно завершеними комітами згідно з конвенцією Conventional Commits:

```text
* e25323a chore: add clang-format configuration file
* e1558fb feat: add systemd service configuration for autostart
* 8c7ec00 fix: output error to stderr when UART interface fails to open
* ee39733 chore: format code
* dcd0393 feat: add echo loop with case inversion and CRLF detection
* 3108a8a feat: implement timestamp-based message logging
* b70caba feat: handle SIGINT/SIGTERM for graceful shutdown
* 8c2525a feat: setup UART interface and command line arguments
* 57015b0 build: add Makefile with CROSS_COMPILE and deploy targets
```

---

## 7. Підсумкові висновки
1. Досліджено специфіку послідовних інтерфейсів на платформі **Raspberry Pi 5 (16 GB RAM)**: враховано розділення на апаратний конектор відладки SoC BCM2712 (`ttyAMA10`) та UART-лінії на 40-піновому роз'ємі GPIO, що керуються чипсетом південного моста **RP1** (`ttyAMA0` / `serial0`).
2. Проведено loopback-тест, що довів працездатність передавача і приймача контролера RP1 на швидкості 115200 бод та наявність прав доступу для непривілейованого користувача.
3. Розроблено та протестовано C-застосунок `mycom`, який через POSIX `termios` здійснює низькорівневе посимвольне читання та інверсію регістру рядків, завершених `\r\n`.
4. Реалізовано крос-компіляцію за допомогою тулчейну `aarch64-linux-gnu-gcc` та автоматизоване розгортання через ціль `deploy` у `Makefile`.
5. Забезпечено надійність роботи програми: створення логів із часовими мітками, діагностика помилок доступу до портів, перехоплення сигналів `SIGINT`/`SIGTERM` для коректного закриття дескрипторів.
6. Створено та перевірено службу `mycom.service` для системи `systemd`, що забезпечує автоматичний запуск програми в режимі демона при завантаженні Raspberry Pi 5.
