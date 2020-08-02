# NBD-Server

## Тест на работоспособность
Создание файловой системы для раздачи:
```
make 
cp path/to/file.txt data-to-backup/file.txt
make create-serverside-fs
```
Запуск nbd-сервера:
```
make run-backup-server
```
Запуск nbd-клиента (в другой консоли):
```
make run-qemu-client
```
Монтирование раздаваемой сервером файловой системы у клиента:
```
make mount-clientside-fs
```
Окончание сессии:
```
make umount-clientside-fs
make stop-backup
```
## Тест на обрыв соединения
Запуск nbd-сервера:
```
make run-backup-server
```
В другой консоли:
```
make test-connection-hangup
```
Этот тест запускает nbd-клиента, после чего временно отключает loopback-интерфейс, которым поддерживалось соединение с nbd-сервером. nbd-сервер посредством механизма TCP-keepalive обнаруживает разрыв соединения (характерное время обнаружения - 5 секунд). По обнаружении утери соединения сервер логирует "Hard disconnect happened".

## Оценка производительности
### Без передачи данных по NBD
```
make mount-serverside-fs
time cp serverside-mount/performance-test-file /dev/null
```
Тест оценивает затраты времени на доступ к диску без учёта издержек протокола NBD.

### Простые ответы
```
make run-backup-server
```
В другой консоли:
```
make run-linux-client
make mount-clientside-fs
time cp clientside-mount/performance-test-file /dev/null
make umount-clientside-fs
make stop-backup
```
Тест оценивает надбавку времени на передачу данных по сети (без учёта нижних уровней OSI). Доступ к диску производится в блокирующем (последовательном) режиме.

### Структурированные ответы
```
make run_backup_server
```
В другой консоли:
```
make run-qemu-client
make mount-clientside-fs
time cp clientside-mount/performance-test-file /dev/null
make umount-clientside-fs
make stop-backup
```
Этот тест, в отличие от предыдущего, производит доступ к диску в асинхронном режиме, что позволяет оценить степень параллельности доступа к диску.

