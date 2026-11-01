# Quy tắc tie-break (OPRTS-36)

Khi hai job có cùng mức ưu tiên, chọn theo thứ tự cố định để trace tái lập được.

| Policy | Khóa so sánh (nhỏ hơn thì thắng) |
|---|---|
| EDF | (absolute_deadline, release_time, task_id) |
| RM | (period, release_time, task_id) |

Ví dụ: A(1,4) và B(1,4) cùng deadline và cùng release thì chọn A (task_id nhỏ hơn).
Cùng input, policy và tie-break thì trace phải giống nhau giữa các lần chạy.
