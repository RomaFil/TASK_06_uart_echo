# TASK 06: ECHO-застосунок через UART

Автор: Роман Філоненко. Дата виконання: 13.10.2026. Завдання Ajax Embedded Linux Internship.

## 1. Мета і що зроблено

Мета: написати ECHO-застосунок, який спілкується через UART (115200 8N1), зібрати його крос-компілятором на HOST, запустити на TARGET і налаштувати автозапуск.

Зроблено:
- Увімкнено UART на 40-піновому роз'ємі Raspberry Pi 5 (`/dev/ttyAMA0`) і перевірено loopback-тестом.
- Написано програму `mycom` (`main.c`, мова C, `termios.h`, `signal.h`): приймає рядок, відповідає тим самим текстом зі зміненим регістром, веде лог із мітками часу.
- Написано `Makefile` з аргументом `CROSS_COMPILE` і цілями `all`, `clean`, `deploy`.
- Перевірено обмін з комп'ютером через USB-UART адаптер PL2303 і PuTTY, а також помилки й сигнали.
- Налаштовано автозапуск через systemd і перевірено після перезавантаження.

## 2. Середовище

| | HOST | TARGET |
|---|---|---|
| Пристрій | ноутбук, Windows 11, WSL2 | Raspberry Pi 5, 16 ГБ |
| ОС | Ubuntu 26.04 LTS | Debian 13 (Raspberry Pi OS) |
| Ядро | 6.18.33.2-microsoft-standard-WSL2 | 6.18.50+rpt-rpi-2712 |
| Архітектура | x86_64 | aarch64 |
| gcc | 15.2.0 (Ubuntu 15.2.0-16ubuntu1); `aarch64-linux-gnu-gcc` 15.2.0 | 14.2.0 (Debian 14.2.0-19) |
| glibc | 2.43 | 2.41 (Debian GLIBC 2.41-12+rpt1+deb13u4) |
| Інше | GNU Make 4.4.1, git 2.53.0, clang-format 23.1.1 | systemd |

Версії TARGET зняті командою `uname -r; gcc --version | head -1; ldd --version | head -1` на Pi 13.10.2026.

### Схема підключення (етап 4)

| Raspberry Pi 5 | USB-UART адаптер PL2303GS |
|---|---|
| пін 8 (GPIO14, TXD0) | RX |
| пін 10 (GPIO15, RXD0) | TX |
| пін 6 (GND) | GND |

Живлення адаптера до Pi не підключалось, рівень сигналів 3,3 В. На ноутбуці адаптер видно як `COM15`, термінал PuTTY: Serial, 115200, 8 біт, 1 стоп-біт, без парності, Flow control None.

## 3. Як увімкнено UART і loopback-тест

Перевірка на Pi (результат у `examples/loopback.txt`):

```
$ cat /boot/firmware/cmdline.txt
console=tty1 root=PARTUUID=a438d780-02 rootfstype=ext4 fsck.repair=yes rootwait quiet splash plymouth.ignore-serial-consoles ...
$ id
uid=1000(roman) gid=1000(roman) groups=1000(roman),4(adm),20(dialout),...
$ ls -l /dev/ttyAMA* /dev/serial*
lrwxrwxrwx 1 root root          8 Oct 13 13:01 /dev/serial0 -> ttyAMA10
crw-rw---- 1 root dialout 204, 64 Oct 13 13:08 /dev/ttyAMA0
crw-rw---- 1 root dialout 204, 74 Oct 13 13:01 /dev/ttyAMA10
$ pinctrl get 14,15
14: a4    pn | hi // GPIO14 = TXD0
15: a4    pu | hi // GPIO15 = RXD0
```

- У `cmdline.txt` немає `console=serial0,...` чи `console=ttyAMA0,...`, тож системна консоль порт не займає.
- Користувач `roman` у групі `dialout`, і всі команди нижче виконувались без `sudo`.
- `/dev/serial0` вказує на `ttyAMA10` (це відлагоджувальний роз'єм, не пін 8/10), тому в `mycom` завжди вказується `/dev/ttyAMA0` явно.
- UART на цих пінах був увімкнений ще 12.10.2026 через `raspi-config` (login shell: No, hardware serial: Yes). Цього разу змін у системі не робилось.

Loopback: один дріт між пінами 8 і 10, адаптер не підключений. Команди:

```bash
unset line
stty -F /dev/ttyAMA0 115200 raw -echo clocal
exec 3<>/dev/ttyAMA0
echo "hello loopback" >&3
read -t 2 -r line <&3; echo "got: $line"
exec 3>&-
```

| Дріт 8↔10 | Результат |
|---|---|
| прибрано | `got:` (порожньо) |
| вставлено | `got: hello loopback` |

Тест із прибраним дротом доводить, що перевірка не «бреше».

## 4. Опис застосунку

Файли: `main.c` (весь код), `Makefile`, `.clang-format`, `mycom.service`, `examples/` (логи й результати тестів).

### Алгоритм

1. Якщо немає аргументу, друкує `Usage: mycom <device>` і завершується з кодом 1.
2. Створює лог-файл в поточній теці. Ім'я = час запуску у форматі `YYYY-MM-DD_HH-MM-SS.log`. Перший запис: `started`.
3. Ставить обробники SIGINT і SIGTERM (`sigaction`).
4. Відкриває порт `open(dev, O_RDWR | O_NOCTTY)`.
5. Зберігає старі налаштування (`tcgetattr`), ставить 115200 8N1 без керування потоком у raw-режимі (`cfmakeraw`, `CLOCAL | CREAD`, `VMIN=0`, `VTIME=1`), очищає буфери (`tcflush`).
6. У циклі читає байти, збирає рядок до 255 символів. Коли приходить кінець рядка:
   - записує в лог `[YYYY-MM-DD HH:MM:SS.mmm] <текст>`;
   - міняє регістр лише ASCII-літер (a↔A), решту символів лишає;
   - відправляє текст і `\r\n` (цикл `write` до повної відправки, з обробкою `EINTR`).
7. Після сигналу виходить із циклу, пише в лог `stopped by signal N`, відновлює старі налаштування порту (`tcsetattr`), закриває порт і лог.

### Прийняті рішення

- **Кінець рядка:** крім `\r\n` програма приймає одинокий `\r` або одинокий `\n`, бо Enter у PuTTY надсилає лише `\r`. Це свідоме відхилення від умови (там `\r\n`). Порожні рядки пропускаються, а `\r\n` не дає двох відповідей, бо `\n` після `\r` бачить порожній рядок.
- **Довгі рядки:** рядок довший за 255 символів обрізається, у лозі додається `(line truncated)`.
- **Регістр:** лише ASCII (`a`–`z`, `A`–`Z`), щоб не псувати кирилицю в UTF-8.
- **Лог:** після кожного запису `fflush`, щоб файл був цілим при аварійній зупинці.
- **Цикл не зависає:** `VMIN=0, VTIME=1` змушує `read` повертатись кожні 0,1 с, тож прапорець сигналу перевіряється вчасно. `SA_RESTART` не використано.
- **Обробник сигналів** лише ставить прапорці `volatile sig_atomic_t` (`g_stop`, `g_sig`), без `printf`/`malloc`.

### Помилки

Спершу створюється лог, потім відкривається порт. Будь-яка проблема (немає пристрою, немає прав, порт не є терміналом тощо): повідомлення з `strerror(errno)` на екран (stderr), той самий рядок у лог, вихід із кодом 1.

### Формат логу

```
[2026-10-13 15:18:12.959] started
[2026-10-13 15:18:12.960] port opened
[2026-10-13 15:18:42.163] Hello Hello World
[2026-10-13 15:20:20.855] stopped by signal 2
```

## 5. Makefile

Змінні: `CROSS_COMPILE` (за замовчуванням порожня), `CC := $(CROSS_COMPILE)gcc`, `CFLAGS ?= -Wall -Wextra -O2 -g`, `LDFLAGS ?=`, `TARGET_USER ?= roman`, `TARGET_HOST ?= pi5.local`, `TARGET_DIR ?= ~/`. Цілі: `all` (за замовчуванням), `clean`, `deploy` (`scp`), `.PHONY`.

Приклади:

```bash
make                                                  # нативна збірка (x86_64)
make clean
make CROSS_COMPILE=aarch64-linux-gnu- LDFLAGS=-static # крос-збірка для Pi
make deploy CROSS_COMPILE=aarch64-linux-gnu- LDFLAGS=-static TARGET_HOST=192.168.10.122
```

Результат `file mycom` для цільового бінарника, скопійованого на Pi:

```
/home/roman/mycom: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=f71294d1636ddbe9b14ae79712436b8e6641d796, for GNU/Linux 3.7.0, with debug_info, not stripped
-rwxr-xr-x 1 roman roman 869384 Oct 13 15:06 /home/roman/mycom
```

Нативна збірка на HOST (`make && file mycom`):

```
gcc -Wall -Wextra -O2 -g  -o mycom main.c
mycom: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=4b00ec49b65f9efc18507465495947ea1344ebd8, for GNU/Linux 3.2.0, with debug_info, not stripped
```

Збірка без попереджень (`-Wall -Wextra`).

### Чому `-static` (знахідка)

Динамічний aarch64-бінарник з HOST вимагає версію бібліотеки `GLIBC_2.42`:

```
$ aarch64-linux-gnu-objdump -T mycom | grep -E 'GLIBC_2\.(42|34)'
... (GLIBC_2.34) __libc_start_main
... (GLIBC_2.42) cfsetispeed
... (GLIBC_2.42) cfsetospeed
```

На HOST glibc 2.43, на TARGET 2.41 (див. розділ 2), тому такий бінарник на Pi не запускається. Перевірено на Pi: динамічна збірка (87 КБ) скопійована на TARGET й запущена вручну при зупиненому сервісі:

```
roman@pi5:~ $ ./mycom /dev/ttyAMA0; echo "rc=$?"
./mycom: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.42' not found (required by ./mycom)
rc=1
```

Тому для TARGET збираю із `-static` (869 КБ проти 89 КБ динамічного): бінарник не залежить від бібліотек Pi.

## 6. Результати тестів

Тести на Pi 5 з адаптером PL2303 (PuTTY, COM15, 115200). Логи лежать у `examples/`.

| Сценарій | Очікувано | Отримано |
|---|---|---|
| `Hello Hello World` + Enter | `hELLO hELLO wORLD` | `hELLO hELLO wORLD` (скрін PuTTY, лог `15-18-12`) |
| `Abc-123 !?` + Enter | `aBC-123 !?` | `aBC-123 !?` |
| `d`, `dfsfsdfd`, `sdfsdf` | великі літери | `D`, `DFSFSDFD`, `SDFSDF` |
| рядок із 300 літер `a` | відповідь обрізана | у лозі рядок з `(line truncated)`, у PuTTY відповідь з великих `A` |
| кілька рядків поспіль | окрема відповідь на кожен | окремі записи в лозі (`15:19:05`–`15:19:08`), окремі відповіді в PuTTY |
| рядок з одного пробілу | відповідь із пробілу | запис із порожнім виглядом у лозі (`15:19:01.964`) |
| без аргументу | `Usage`, код ≠ 0 | `Usage: mycom <device>`, `rc=1` |
| `/dev/ttyXYZ` | помилка, код ≠ 0 | `ERROR: cannot open /dev/ttyXYZ: No such file or directory`, `rc=1` |
| файл без прав (`/tmp/noperm`, `chmod 000`) | помилка, код ≠ 0 | `ERROR: cannot open /tmp/noperm: Permission denied`, `rc=1` |
| `/dev/null` (не термінал) | помилка, код ≠ 0 | `ERROR: cannot configure /dev/null: Inappropriate ioctl for device`, `rc=1` |
| Ctrl+C (SIGINT) | порт відновлено, лог закрито | `stopped by signal 2` у лозі й на екрані |
| `kill -TERM` (SIGTERM) | те саме | `stopped by signal 15`, `rc=0` (лог `examples/2026-10-13_15-22-01.log`) |
| відновлення порту | швидкість повернена | перед запуском `stty -F /dev/ttyAMA0 9600`, після Ctrl+C `speed 9600 baud` |

Для кожної помилки повідомлення з'являється на екрані, а також записується в лог (`examples/2026-10-13_15-20-31.log`).

Не перевірялось окремо: натискання Enter без жодного символу (у коді такий рядок пропускається, `len == 0`); зайнятий порт (захист `TIOCEXCL` не реалізовано, див. розділ 9).

Фрагмент лога (SIGTERM):

```
[2026-10-13 15:22:01.530] started
[2026-10-13 15:22:01.530] port opened
[2026-10-13 15:22:07.026] stopped by signal 15
```

## 7. Автозапуск (systemd)

Система ініціалізації на Pi: `ps -p 1 -o comm=` → `systemd`.

Файл `/etc/systemd/system/mycom.service` (копія в репозиторії):

```ini
[Unit]
Description=UART echo application (mycom)
After=multi-user.target

[Service]
Type=simple
User=roman
WorkingDirectory=/home/roman/mycom-logs
ExecStart=/home/roman/mycom /dev/ttyAMA0
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Команди:

```bash
mkdir -p ~/mycom-logs
sudo cp ~/mycom.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mycom
systemctl status mycom --no-pager
```

Результат: `Loaded: ... enabled`, `Active: active (running)`, процес `/home/roman/mycom /dev/ttyAMA0`, лог створився в `~/mycom-logs`. Відповіді на повідомлення з PuTTY приходять без ручного запуску, як **до**, так і **після** `sudo reboot` (після перезавантаження сервіс стартував о 15:31:59, у `~/mycom-logs` з'явився новий лог). Логи сервісу: `examples/service_before_reboot.log` (до перезавантаження), `examples/service_after_reboot.log` (після).

Спостереження: `printf` не потрапляє в журнал одразу, бо коли вивід йде не в термінал, C-бібліотека його буферизує. У `journalctl -u mycom` повідомлення з'явились лише при зупинці сервісу (15:40:09), разом із `stopped by signal 15`; `systemctl stop` завершився чисто (`Deactivated successfully`), що підтверджує обробку SIGTERM під systemd:

```
Oct 13 15:40:09 pi5 mycom[1301]: Listening on /dev/ttyAMA0. Press Ctrl+C to stop.
Oct 13 15:40:09 pi5 mycom[1301]: stopped by signal 15
Oct 13 15:40:09 pi5 systemd[1]: Stopping mycom.service - UART echo application (mycom)...
Oct 13 15:40:09 pi5 systemd[1]: mycom.service: Deactivated successfully.
```

Для миттєвого появлення в журналі вистачило б `fflush(stdout)`, але лог-файл повний і без цього. Ручний запуск і сервіс не поєднуються (порт один): для ручних тестів потрібен `sudo systemctl stop mycom`.

### BusyBox init (теоретично, на залізі не перевірено)

У Buildroot-образі (потрібен у фіналі стажування) замість systemd працює BusyBox init, який запускає скрипти `S??*` з `/etc/init.d/` по порядку. Скрипт `/etc/init.d/S99mycom` (`chmod +x`):

```sh
#!/bin/sh
case "$1" in
  start)
    start-stop-daemon -S -b -m -p /var/run/mycom.pid -x /usr/bin/mycom -- /dev/ttyAMA0 ;;
  stop)
    start-stop-daemon -K -p /var/run/mycom.pid ;;
  restart)
    "$0" stop; "$0" start ;;
  *)
    echo "Usage: $0 {start|stop|restart}"; exit 1 ;;
esac
```

Умова: у конфігурації BusyBox образу має бути ввімкнений `start-stop-daemon`. Цей скрипт я не запускав.

## 8. Проблеми й висновки

- **Бібліотека glibc:** крос-компілятор HOST новіший за систему TARGET (glibc 2.43 проти 2.41). Динамічний бінарник вимагав `GLIBC_2.42` (`cfsetispeed`/`cfsetospeed`). Вирішення: статична лінковка. Висновок: версії бібліотек HOST і TARGET треба порівнювати до деплою (так само, як у TASK 04).
- **`pi5.local` не знаходиться з WSL** (mDNS не працює), `scp` падав із `Could not resolve hostname`. Вирішення: `TARGET_HOST=192.168.10.122` (IP видає DHCP, тому може змінитись).
- **`.clang-format` від clang-format 23:** системна версія 21 не читає ключі `EnumAssignments` і `AlignPPAndNotPP` (`unknown key`). Вирішення: форматування й перевірка версією 23 з venv (`~/.venvs/clang23`). Це та сама проблема версій конфігу, що й у TASK 03.
- **UART мовчав або давав шум:** на початку етапу 4 і після нього тиша та випадкові символи. Допомогла повторна перевірка дротів і адаптера (спершу адаптер сам на себе TX↔RX, потім підключення до Pi). Точної причини сьогодні я не зафіксував. Раніше, 12.10.2026, таку ж тишу давала гребінка модуля PL2303, зсунута на один пін. Загальні умови: TX↔RX навхрест, спільний GND, лише 3,3 В, однакова швидкість на обох боках.
- **PuTTY:** за замовчуванням швидкість 9600, потрібно 115200. Слідкувати за Caps Lock: через нього відповідь здавалась «неправильною» (ввід `TEST` великими дає відповідь `test`). Local echo у PuTTY змішує набране з відповідями, тому для чистих тестів його краще вимкнути.
- **Рядок без `\r`:** `echo "hello from pi" > /dev/ttyAMA0` надсилає лише `\n`, тому PuTTY показав текст «сходинкою». Відповіді `mycom` завершуються `\r\n` і цим не страждають.
- **Шум на лінії:** у лозі сервісу є рядок із сміттєвих байтів (`15:29:15`), а при перезавантаженні Pi у PuTTY з'явилась смуга символів. Імовірно, це шум на лінії (дроти, завантаження Pi), а не помилка програми; точну причину я не з'ясовував.
- **Чому не можна запускати ECHO на loopback:** програма отримає власну відповідь, відповість на неї, і так нескінченно. Я цього не пробував, лише вказав у плані.
- **Дрібні помилки в процесі:** зайвий `return 0;` у `main`, помилка `]` в команді `exec 3<>/dev/ttyAMA0`, `scp` виконано на самій Pi замість WSL. Усе виправлено.

## 9. Що можна покращити

- Захист `ioctl(fd, TIOCEXCL)`, щоб другий запуск на тому ж порту одразу давав `Device or resource busy`.
- `fflush(stdout)`, щоб повідомлення потрапляли в `journalctl`.
- Швидкість як другий аргумент (`argv[2]`) і `poll()` замість `VTIME`.
- GPS: окремий порт зі швидкістю 9600 (модуль GPS видає NMEA на 9600). На платі Motion Logger його заплановано на `uart2` (GPIO4/5), `dtoverlay` для цього ще не перевірений, тож у цьому завданні GPS не підключався.
- Перевірити скрипт BusyBox init на справжньому Buildroot-образі.
