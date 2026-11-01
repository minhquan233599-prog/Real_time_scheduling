# Mô hình tác vụ thời gian thực (OPRTS-36)

## Giả định đã khóa
- Một CPU, preemptive, các task độc lập, không tự suspend giữa job.
- Chi phí context switch bằng 0 trong mô hình toán.
- Periodic task, offset O = 0, deadline implicit D = T.
- Đơn vị thời gian là tick rời rạc. Một tick không mặc định bằng 1 ms.

## Ký hiệu
| Ký hiệu | Ý nghĩa |
|---|---|
| C | execution time / runtime budget của mỗi job |
| T | period (chu kỳ) |
| D | relative deadline, D = T |
| r | release time của job |
| d = r + D | absolute deadline |
| f | finish time |
| U = C/T | utilization; tổng U = Σ Ci/Ti |

## Task và job
Task là mẫu công việc lặp lại. Job là một lần thực hiện cụ thể (A1, A2, ...) có release và deadline riêng.
Scheduler chọn job, còn C và T là thông số của task.

## Đúng hạn và miss
- Job đúng hạn khi f <= d. Job xong đúng t = d vẫn đúng hạn.
- Tại t = d mà job còn việc chưa xong thì tính 1 deadline miss.
- Mỗi job chỉ bị đếm miss tối đa một lần (miss_recorded theo job_id).

## Chính sách job trễ
DROP-LATE: tại deadline đánh dấu miss một lần và loại job khỏi hàng đợi.

## Thứ tự sự kiện tại mỗi tick t
1. Phát hiện job chưa xong có d <= t, ghi miss đúng một lần, drop.
2. Release các job đến hạn tại t.
3. Chọn job theo policy (EDF hoặc RM).
4. Chạy 1 tick. Nếu remaining = 0 thì finish time = t + 1.

## API
sched_setdeadline(period, runtime) là API do nhóm tự xây trong xv6, không phải syscall Linux.
Mã trả về: 0 = OK, -1 = tham số không hợp lệ, -2 = admission reject.
Điều kiện hợp lệ: period > 0, runtime > 0, runtime <= period.
