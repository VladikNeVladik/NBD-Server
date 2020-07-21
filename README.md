# NBD-Server

## Тест на работоспособность
Первичный запуск бэкап-сервера:
```
make install
cp /path/to/valuable-file.txt data-to-backup/valuable-file.txt
make run_backup_server
```
Запуск бэкап-клиента (в другой консоли):
```
make run_qemu_client
cat storage/valuable-file.txt
```
Окончание сессии:
```
make stop_backup
```
## Тест на обрыв соединения
Запуск бэкап-сервера:
```
make run_backup_server
```
В другой консоли:
```
make test_connection_hangup
```
Этот тест запускает бэкап-клиента, после чего временно отключает loopback-интерфейс, которым поддерживалось соединение с бэкап-сервером. Бэкап-сервер посредством механизма TCP-keepalive обнаруживает разрыв соединения (характерное время обнаружения - 5 секунд). По обнаружении утери соединения сервер логирует "Hard disconnect happened".

## Оценка производительности
### Монтирование файловой системы
```
make run_backup_server
```
В другой консоли:
```
time make run_plain_mount
umount storage
```
Тест оценивает затраты времени на доступ к диску без учёта издержек протокола NBD.

### Простые ответы
```
make run_backup_server
```
В другой консоли:
```
time make run_linux_client
make stop_backup
```
Тест оценивает надбавку времени на передачу данных по сети (без учёта нижних уровней OSI). Доступ к диску производится в блокирующем (последовательном) режиме.

### Структурированные ответы
```
make run_backup_server
```
В другой консоли:
```
time make run_qemu_client
make stop_backup
```
Этот тест, в отличие от предыдущего, производит доступ к диску в асинхронном режиме, что позволяет оценить степень параллельности доступа к диску.

