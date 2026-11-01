# Đường đi system call trong xv6 (OPRTS-44)

Học theo syscall có sẵn `uptime` trên commit hiện tại của repo nhóm. Số dòng lấy từ lệnh `git grep -n` và `Get-Content` chạy trên máy, ngày 01/11/2026.

## 1. Kết quả git grep

```
kernel/syscall.c:97:extern uint64 sys_uptime(void);
kernel/syscall.c:124:  [SYS_uptime]  = sys_uptime,
kernel/syscall.h:15:#define SYS_uptime 14
kernel/sysproc.c:104:sys_uptime(void)
user/user.h:26:int uptime(void);
user/usys.pl:44:entry("uptime");
```

Số hiệu syscall cuối cùng hiện có là `SYS_sync 22` (kernel/syscall.h:23), nên số trống kế tiếp là 23.

## 2. Năm điểm chạm của một syscall

| Bước | File:dòng | Với `uptime` | Với API của đề tài (dự kiến) |
|---|---|---|---|
| 1. Prototype cho user | user/user.h:26 | `int uptime(void);` | `int sched_setdeadline(int, int);` và `int rt_job_done(void);` |
| 2. Stub user | user/usys.pl:44 | `entry("uptime");` | `entry("sched_setdeadline");` và `entry("rt_job_done");` |
| 3. Số hiệu syscall | kernel/syscall.h:15 | `#define SYS_uptime 14` | `#define SYS_sched_setdeadline 23` và `#define SYS_rt_job_done 24` |
| 4. Bảng dispatch | kernel/syscall.c:97 (extern), :124 (bảng) | `[SYS_uptime] = sys_uptime,` | thêm hai dòng `extern` và hai dòng `[SYS_...] = sys_...,` |
| 5. Hàm xử lý | kernel/sysproc.c:104 | `sys_uptime(void)` | `sys_sched_setdeadline()` và `sys_rt_job_done()` |

## 3. Luồng chạy một lần gọi

1. Chương trình user gọi `uptime()`.
2. Stub do `usys.pl` sinh ra nạp số hiệu 14 vào thanh ghi rồi thực hiện `ecall`.
3. CPU chuyển vào kernel qua trap; hàm `syscall()` trong kernel/syscall.c đọc số hiệu.
4. `syscall()` tra bảng dispatch (dòng 124) và gọi `sys_uptime()`.
5. Giá trị trả về được ghi vào thanh ghi kết quả và quay lại user.

## 4. Cách đọc đối số trong kernel

Đối số được đọc bằng `argint(n, &biến)`, định nghĩa ở kernel/syscall.c:58 (`argint(int n, int *ip)`). Ở commit này hàm không trả giá trị, các chỗ gọi trong sysproc.c đều không kiểm tra lỗi.

| Số đối số | Hàm mẫu | File:dòng | Cách gọi |
|---|---|---|---|
| 2 | `sys_sbrk()` | kernel/sysproc.c:40 (đối số ở dòng 46-47) | `argint(0, &n); argint(1, &t);` |
| 1 | `sys_pause()` | kernel/sysproc.c:73 | `argint(0, &n);` |
| 1 | `sys_kill()` | kernel/sysproc.c:93 (đối số ở dòng 97) | `argint(0, &pid);` |
| 1 | hàm ở kernel/sysproc.c:14 | kernel/sysproc.c:14 | `argint(0, &n);` (tên hàm cần đối chiếu) |

Mẫu cho `sched_setdeadline(period, runtime)` là `sys_sbrk()` vì cùng có hai đối số:

```c
int period, runtime;
argint(0, &period);    // đối số thứ nhất: period
argint(1, &runtime);   // đối số thứ hai: runtime
```

## 5. Dự kiến cho API của đề tài

- `sys_sched_setdeadline()` và `sys_rt_job_done()` đặt trong kernel/sysproc.c (hoặc file mới, thêm vào Makefile).
- Thứ tự tham số giữ đúng đề bài: `(period, runtime)`.
- Vì `argint` không báo lỗi, việc kiểm tra `period > 0`, `runtime > 0`, `runtime <= period` phải viết tường minh trong `sys_sched_setdeadline()`.
- Mã trả về: 0 (OK), -1 (tham số không hợp lệ), -2 (admission reject). Khi lỗi không được thay đổi trạng thái.
- Số hiệu syscall chỉ nối tiếp ở cuối (23, 24), không sửa các số cũ.
- Cần thêm chương trình test trong `user/` và đăng ký vào `UPROGS` của Makefile.

## 6. Phạm vi

Tuần 2 chỉ phân tích; chưa sửa code kernel. Việc cài đặt thuộc Sprint 3.