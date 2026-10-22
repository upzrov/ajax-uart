# Звіт з лабораторної роботи: Розробка ECHO-застосунку для UART інтерфейсу

## Мета роботи

Ознайомлення з апаратною організацією та принципами функціонування послідовного асинхронного інтерфейсу UART в ОС Linux на базі мікрокомп'ютера **Raspberry Pi 5 (16 GB RAM)**. Налаштування та активація апаратного UART-порту на 40-піновому роз'ємі розширення GPIO із врахуванням архітектури чипа введення-виведення RP1. Розробка, крос-компіляція та верифікація C-застосунку для двосторонньої передачі даних (ECHO з інверсією регістру символів) з використанням низькорівневого інтерфейсу `termios.h`, механізму логування з часовими мітками, обробки сигналів завершення (`signal.h`) та налаштування автозапуску через службу `systemd`.

---

## 1. Конфігурація апаратного та програмного середовища

| Параметр                        | HOST (Комп'ютер розробника)                  | TARGET (Цільова плата)                                          |
| :------------------------------ | :------------------------------------------- | :-------------------------------------------------------------- |
| **Пристрій / Платформа**        | Ноутбук (Laptop x86_64)                      | **Raspberry Pi 5 Model B (Rev 1.0)**                            |
| **Процесор / Архітектура**      | Intel Core i5 (x86_64)                       | **Broadcom BCM2712**, 4x ARM Cortex-A76 @ 2.4 GHz (AArch64)     |
| **Контролер I/O (Southbridge)** | Вбудований Intel PCH                         | **Raspberry Pi RP1-C0** (PCIe I/O Controller)                   |
| **Оперативна пам'ять (RAM)**    | **8 GB DDR4**                                | **16 GB LPDDR4X-4267 SDRAM**                                    |
| **Операційна система**          | Ubuntu 24.04 LTS (Linux 6.8.0)               | **Raspberry Pi OS Bookworm 64-bit** (Debian 12)                 |
| **Ядро Linux**                  | 6.8.0-45-generic                             | `6.6.31+rpt-rpi-2712` (з підтримкою 16KB Page Size)             |
| **IP-адреса / Користувач**      | `172.16.217.1` / `dev`                       | `172.16.217.116` / `xrov`                                       |
| **Інструменти компіляції**      | `aarch64-linux-gnu-gcc` 13.2.0               | `gcc` 12.2.0 (native)                                           |
| **UART інтерфейс**              | USB-UART перехідник (CP2102, `/dev/ttyUSB0`) | Вбудований PL011 UART на RP1 (`/dev/serial0` -> `/dev/ttyAMA0`) |

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

- **Швидкість (Baudrate):** 115200 бод
- **Формат кадру:** 8N1 (8 біт даних, No Parity (без перевірки парності), 1 стоп-біт)
- **Контроль потоку (Flow Control):** None (вимкнено)

---

## 2. Завдання 1: Налаштування та активація UART інтерфейсу на TARGET

### 2.1. Особливості архітектури UART на Raspberry Pi 5

В архітектурі **Raspberry Pi 5** підсистема введення-виведення була суттєво модернізована:

1. **Виділений 3-піновий роз'єм Debug UART:** Між двома портами micro-HDMI додано окремий конектор JST-SH для відладки ядра (підключений безпосередньо до BCM2712, пристрій `/dev/ttyAMA10`).
2. **40-піновий роз'єм GPIO:** Піни 8 (GPIO 14) та 10 (GPIO 15) маршрутизуються через фірмовий чипсет південного моста **RP1** (Raspberry Pi RP1-C0), підключений до SoC через інтерфейс 4x PCIe 2.0. Контролер UART на роз'ємі GPIO обслуговується драйвером PL011 і прив'язаний до пристрою `/dev/ttyAMA0` (символічне посилання `/dev/serial0`).

**Розпіновка роз'єму GPIO для UART:**

- **Pin 8 (GPIO 14):** `UART0_TXD` (Передавач RP1)
- **Pin 10 (GPIO 15):** `UART0_RXD` (Приймач RP1)
- **Pin 6 (GND):** Сигнальна земля

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
├── Makefile         # Сценарій збирання з підтримкою CROSS_COMPILE, deploy та install
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

Файл `Makefile` підтримує аргумент `CROSS_COMPILE` для збирання на комп'ютері розробника для архітектури AArch64 (Raspberry Pi 5), віддалену доставку (`deploy`), повноцінне системне встановлення (`install` / `uninstall`), а також команди керування життєвим циклом служби:

```makefile
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mycom
SRC = main.c
SERVICE = mycom.service

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSTEMD_DIR ?= /etc/systemd/system

DEPLOY_USER ?= xrov
DEPLOY_HOST ?= raspberrypi.local
DEPLOY_PATH ?= /home/$(DEPLOY_USER)/

.PHONY: all clean deploy install uninstall install-remote start stop restart status

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.log

deploy: $(TARGET) $(SERVICE)
	@if [ -z "$(DEPLOY_HOST)" ]; then \
		echo "Error: DEPLOY_HOST is not set. Usage: make deploy DEPLOY_HOST=<ip_or_hostname>"; \
		exit 1; \
	fi
	scp $(TARGET) $(SERVICE) Makefile $(DEPLOY_USER)@$(DEPLOY_HOST):$(DEPLOY_PATH)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(SYSTEMD_DIR)
	install -m 644 $(SERVICE) $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	systemctl enable $(SERVICE)

uninstall:
	-systemctl stop $(SERVICE)
	-systemctl disable $(SERVICE)
	rm -f $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

install-remote: deploy
	ssh -t $(DEPLOY_USER)@$(DEPLOY_HOST) "cd $(DEPLOY_PATH) && sudo make install"

start:
	systemctl start $(SERVICE)

stop:
	systemctl stop $(SERVICE)

restart:
	systemctl restart $(SERVICE)

status:
	systemctl status $(SERVICE)
```

> [!TIP]
> Значення `DEPLOY_HOST ?= raspberrypi.local` використовує mDNS-ім'я мікрокомп'ютера за замовчуванням замість жорстко закодованої статичної IP-адреси. За потреби адреса легко перевизначається в командному рядку (наприклад, `make deploy DEPLOY_HOST=172.16.217.116` або через змінну оточення `export DEPLOY_HOST=...`).

**Процес крос-компіляції на HOST та розгортання:**

```bash
dev@laptop:~/ajax-uart$ make CROSS_COMPILE=aarch64-linux-gnu-
aarch64-linux-gnu-gcc -Wall -Wextra -O2 -o mycom main.c

dev@laptop:~/ajax-uart$ file mycom
mycom: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=f704..., for GNU/Linux 3.7.0, not stripped

dev@laptop:~/ajax-uart$ make deploy
# Використання mDNS (або явне: make deploy DEPLOY_HOST=172.16.217.116)
scp mycom mycom.service Makefile xrov@raspberrypi.local:/home/xrov/
mycom                                       100%   71KB   8.2MB/s   00:00
mycom.service                               100%  350B    1.2MB/s   00:00
Makefile                                    100%  1.1KB   2.8MB/s   00:00
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

Після виходу з основного циклу `while (keep_running)` програма акуратно закриває дескриптор `serial_fd`, робить запис `Сервер зупинено` у файл логу та закриває `log_file`.

---

## 4. Автоматичний запуск застосунку за допомогою systemd

### 4.1. Розробка systemd unit-файлу (`mycom.service`) (Завдання 1)

Для забезпечення роботи ECHO-застосунку у фоновому режимі як системної служби розроблено файл конфігурації `mycom.service`:

```ini
[Unit]
Description=UART Echo Service (mycom) on Raspberry Pi 5
After=sysinit.target
Wants=sysinit.target

[Service]
Type=simple
User=xrov
Group=xrov
SupplementaryGroups=dialout
WorkingDirectory=/home/xrov
ExecStart=/usr/local/bin/mycom /dev/serial0
Restart=on-failure
RestartSec=3s
KillMode=process
KillSignal=SIGTERM
TimeoutStopSec=5s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

#### Детальний аналіз реалізації вимог завдання 1:

- **п. 1.а (Запуск з правильним UART-інтерфейсом):**
  - Директива `ExecStart=/usr/local/bin/mycom /dev/serial0` вказує на системний бінарник `/usr/local/bin/mycom` та передає як параметр пристрій `/dev/serial0` (символічне посилання на PL011 UART0 контролера RP1 на Raspberry Pi 5).
- **п. 1.б (Перезапуск при падінні програми):**
  - Директива `Restart=on-failure` налаштовує перезапуск сервісу у випадку ненульового коду повернення процесу або аварійного завершення сигналом (наприклад `SIGSEGV`, `SIGABRT`).
  - Параметр `RestartSec=3s` задає 3-секундну затримку перед перезапуском, захищаючи від тротлінгу при збоях обладнання.
- **п. 1.в (Запуск після готовності системи до роботи з пристроями):**
  - Використано `After=sysinit.target` та `Wants=sysinit.target`. Системна ціль `sysinit.target` гарантує, що базові файлові системи змонтовано, а менеджер пристроїв `systemd-udevd` завершив ініціалізацію та створив файли пристроїв у каталозі `/dev/` (включно з `/dev/ttyAMA0` та `/dev/serial0`).
  - _Примітка щодо `After=multi-user.target`:_ Оскільки в секції `[Install]` зазначено `WantedBy=multi-user.target`, використання `After=multi-user.target` призводить до виникнення циклічної залежності (ordering cycle), внаслідок якої systemd змушений скасовувати запуск одного з юнітів. Директива `After=sysinit.target` є канонічним і безпечним рішенням для апаратних сервісів.
- **п. 1.г (Штатне завершення при зупинці сервісу):**
  - Параметр `KillSignal=SIGTERM` вказує, що при виклику `systemctl stop mycom` надсилається сигнал `SIGTERM`.
  - У вихідному коді `main.c` встановлено обробник `signal(SIGTERM, handle_signal)`, який встановлює прапорець завершення циклу `keep_running = 0`.
  - Програма завершує блокуюче очікування, закриває дескриптор `serial_fd`, записує рядок `Сервер зупинено` у відкритий файл журналу та повертає код `0` (`EXIT_SUCCESS`).
  - Параметр `TimeoutStopSec=5s` запобігає зависанню та обмежує час очікування завершення перед надсиланням `SIGKILL`.
  - Для дотримання принципу найменших привілеїв зазначено `User=xrov`, `Group=xrov` та `SupplementaryGroups=dialout`, що надає доступ до пристрою `/dev/serial0` (який належить групі `dialout`) без підвищення прав до `root`.

---

### 4.2. Доопрацювання Makefile (ціль `install`) (Завдання 2.а)

У `Makefile` додано змінну конфігурації шляхів та ціль `install`:

```makefile
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSTEMD_DIR ?= /etc/systemd/system
SERVICE = mycom.service

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(SYSTEMD_DIR)
	install -m 644 $(SERVICE) $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	systemctl enable $(SERVICE)
```

**Етапи виконання цілі `install`:**

1. Створення директорії `/usr/local/bin` утилітою `install -d`.
2. Копіювання виконуваного бінарника `mycom` у `/usr/local/bin/mycom` з правами доступу `0755` (`rwxr-xr-x`).
3. Створення системної директорії юнітів `/etc/systemd/system/`.
4. Копіювання unit-файлу `mycom.service` з правами доступу `0644` (`rw-r--r--`).
5. Виконання `systemctl daemon-reload` для оновлення системного реєстру конфігурацій systemd.
6. Вмикання автозапуску служби за допомогою `systemctl enable mycom.service` (створення символічного посилання в `multi-user.target.wants`).

Крім цього, додано ціль `uninstall` для коректного видалення та `install-remote` для зручного розгортання та інсталяції з HOST-машини.

---

### 4.3. Перевірка функціонування (Завдання 2.б)

#### 1. Встановлення та запуск служби:

```bash
xrov@raspberrypi:~$ sudo make install
install -d /usr/local/bin
install -m 755 mycom /usr/local/bin/mycom
install -d /etc/systemd/system
install -m 644 mycom.service /etc/systemd/system/mycom.service
systemctl daemon-reload
systemctl enable mycom.service
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
             └─1845 /usr/local/bin/mycom /dev/serial0
```

#### 2. Перевірка автозапуску після перезавантаження плати (Boot check):

```bash
xrov@raspberrypi:~$ sudo reboot
# Після повторного з'єднання через SSH:
xrov@raspberrypi:~$ systemctl is-enabled mycom.service
enabled

xrov@raspberrypi:~$ systemctl is-active mycom.service
active

xrov@raspberrypi:~$ ps aux | grep mycom
xrov        642  0.0  0.0   2712  1420 ?        Ss   17:20   0:00 /usr/local/bin/mycom /dev/serial0
```

_Результат:_ Служба автоматично стартує при запуску ОС без участі користувача.

#### 3. Перевірка роботи у фоновому режимі та ECHO:

З комп'ютера розробника (HOST) через термінал послідовного порту надсилаються дані:

```text
dev@laptop:~$ picocom -b 115200 --omap crlf /dev/ttyUSB0
Terminal ready

Systemd Background UART Echo Test
sYSTEMD bACKGROUND uart eCHO tEST
```

Перевірка журналу на цільовій платі:

```bash
xrov@raspberrypi:~$ tail -n 5 /home/xrov/20261011_172015.log
Сервер запущено на /dev/serial0
[2026-10-11 17:20:45] Отримано: Systemd Background UART Echo Test
```

#### 4. Перевірка штатної зупинки (`systemctl stop`):

```bash
xrov@raspberrypi:~$ sudo systemctl stop mycom.service
xrov@raspberrypi:~$ systemctl status mycom.service
○ mycom.service - UART Echo Service (mycom) on Raspberry Pi 5
     Loaded: loaded (/etc/systemd/system/mycom.service; enabled; preset: enabled)
     Active: inactive (dead) since Sun 2026-10-11 17:22:00 EEST; 2s ago
    Process: 642 ExecStart=/usr/local/bin/mycom /dev/serial0 (code=exited, status=0/SUCCESS)
   Main PID: 642 (code=exited, status=0/SUCCESS)

xrov@raspberrypi:~$ tail -n 2 /home/xrov/20261011_172015.log
[2026-10-11 17:20:45] Отримано: Systemd Background UART Echo Test
Сервер зупинено
```

_Результат:_ Процес завершився з кодом `0` (`SUCCESS`), лог-файл коректно закрито із записом `Сервер зупинено`.

#### 5. Перевірка перезапуску (`systemctl restart`):

```bash
xrov@raspberrypi:~$ sudo systemctl restart mycom.service
xrov@raspberrypi:~$ systemctl status mycom.service
● mycom.service - UART Echo Service (mycom) on Raspberry Pi 5
     Loaded: loaded (/etc/systemd/system/mycom.service; enabled; preset: enabled)
     Active: active (running) since Sun 2026-10-11 17:23:10 EEST; 1s ago
   Main PID: 1912 (mycom)
      Tasks: 1 (limit: 16384)
     Memory: 668.0K
        CPU: 4ms
     CGroup: /system.slice/mycom.service
             └─1912 /usr/local/bin/mycom /dev/serial0
```

_Результат:_ Сервіс оперативно перезапущено зі збереженням працездатності.

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

_Результат:_ Кожне повідомлення, завершене послідовністю `\r\n`, коректно інвертується за регістром (малі літери перетворюються у великі, великі — у малі).

### 5.3. Перевірка згенерованого лог-файлу на TARGET

```bash
xrov@raspberrypi:~$ ls -l *.log
-rw-r--r-- 1 xrov xrov 276 Oct 11 17:16 20261011_171530.log

xrov@raspberrypi:~$ cat 20261011_171530.log
Сервер запущено на /dev/serial0
[2026-10-11 17:16:04] Отримано: Hello World!
[2026-10-11 17:16:18] Отримано: Raspberry Pi 5 16GB - UART Echo Active
[2026-10-11 17:16:32] Отримано: Ajax Systems Lab 2026
Сервер зупинено
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

_Результат:_ Програма фіксує помилку одночасно у терміналі (потік `stderr`) та у щойно створеному файлі журналу, після чого повертає ненульовий код помилки `1`.

### 5.5. Тестування штатного завершення за сигналами

При виклику `sudo systemctl stop mycom` (надсилання сигналу `SIGTERM`) або натисканні `Ctrl+C` у консолі:

```bash
xrov@raspberrypi:~$ ./mycom /dev/serial0
^C
xrov@raspberrypi:~$ echo $?
0
```

У файлі логу гарантовано з'являється завершальний запис `Сервер зупинено`, дескриптор пристрою звільняється в ядрі.

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
* c7057e2 feat: add systemd autostart service and Makefile install target
* 1d2cd95 docs: add report
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
4. Реалізовано крос-компіляцію за допомогою тулчейну `aarch64-linux-gnu-gcc` та розширено автоматизацію у `Makefile` (цілі `deploy`, `install`, `uninstall`, `install-remote`).
5. Забезпечено надійність роботи програми: створення логів із часовими мітками, діагностика помилок доступу до портів, перехоплення сигналів `SIGINT`/`SIGTERM` для коректного закриття дескрипторів.
6. Створено та налаштовано службу `mycom.service` для системи `systemd`: забезпечено запуск після готовності підсистеми пристроїв (`After=sysinit.target`), автоматичний перезапуск при аварійних збоях (`Restart=on-failure`, `RestartSec=3s`) та штатне завершення за сигналом `KillSignal=SIGTERM`.
7. Перевірено повний цикл функціонування сервісу: автоматичний запуск при старті операційної системи, безперебійна обробка послідовних даних у фоновому режимі, коректне керування через `systemctl stop` та `systemctl restart`.
