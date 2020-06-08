# NBD-Server

### Как запусть тест (0_0)
```
  make install
  cp /path/to/file.txt data-to-backup/file.xtx
  make run_backup_server
```
В другой консоли:
```
  make linux_client
  cat storage/file.txt
  make stop_backup
```

Для окончания бэкапа: 
```
  make stop_backup
```

### Что есть и работает (^_^)
- Non-fixed newstyle negotiation
- NOTLS mode
- Simple replies to CMD_NBD_READ (кажется, даже zero-copy)
- NBD_CMD_DISC

### Что есть и не работает (-_-)
- Непонятно, как запустить qemu-nbd (упоминаемого в интернетах флажка -L нету ни в man-ах, ни в qemu-nbd --help-е)
- Написаннные, но, увы, не отлаженные Structured Replies через IO-Userspace-Ring

### Что плохо (X_X)
- Отсутсвует любая параллельность, и потому не используется большая часть вычислительного ресурса (в один момент времени всегда простаивает либо хранилище данных (которое, уж наверняка, способно на большое, чем 1, количество единовременных чтений-записи), либо сетевая карта (аналогично), используется только одно ядро)
- Единовременно может обрабатываться только один запрос (таков уж формат simple reply) => low latency

