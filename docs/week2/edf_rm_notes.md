# Ghi chú EDF và RM/RTA — Tuần 2

Phạm vi: OPRTS-37 và OPRTS-38.
Nội dung nghiên cứu lý thuyết và đối chiếu simulator; chưa triển khai
EDF/RM trong kernel xv6.

## 1. OPRTS-37 — Earliest Deadline First (EDF)

### Định nghĩa và mô hình áp dụng

EDF là thuật toán ưu tiên động: trong các job sẵn sàng, chọn job có
deadline tuyệt đối sớm nhất.

Với job phát hành tại thời điểm r, deadline tuyệt đối là d = r + D.
Trong mô hình của nhóm, D = T nên d = r + T.

Các giả định: một CPU, task định kỳ và độc lập, cho phép preemption,
không tự suspend, không có blocking, không tính overhead; offset O = 0.
C và T là số tick nguyên, thỏa mãn 0 < C <= T.

Một job hoàn thành đúng hạn khi finish_time <= deadline.
Hoàn thành đúng tại deadline không bị tính là miss.

### Quy tắc lựa chọn

Chỉ xét job đã release, chưa hoàn thành, chưa bị drop và còn công việc.
So sánh theo thứ tự:

1. Deadline tuyệt đối nhỏ hơn.
2. Nếu bằng nhau: release time nhỏ hơn.
3. Nếu vẫn bằng nhau: task ID nhỏ hơn.

Quy tắc tie-break cố định giúp kết quả mô phỏng tái lập được.
Job mới có deadline sớm hơn có thể giành CPU tại điểm lập lịch hợp lệ.

### Điều kiện admission

Tính tổng mức sử dụng CPU của toàn bộ tập task sau khi thêm task mới:

$$
U = \sum_{i=1}^{n}\frac{C_i}{T_i}
$$

Trong mô hình một CPU, preemptive, các task độc lập, D = T và
overhead bằng 0, điều kiện U <= 1 là cần và đủ để EDF lập lịch khả thi.

- Tham số không hợp lệ: INVALID.
- U > 1: REJECT_OVERLOAD.
- Tham số hợp lệ và U <= 1: ACCEPT.

Không áp dụng kết luận này trực tiếp cho D < T hoặc kernel có
overhead/blocking chưa được tính đến.

### Ví dụ EDF chấp nhận tập EDF_ONLY

Cho A = (C=2, T=5), B = (C=4, T=7):

$$
U = \frac{2}{5}+\frac{4}{7}=\frac{34}{35}\approx0.9714 < 1
$$

EDF chấp nhận tập task trong mô hình đã nêu.

Lịch ban đầu: A1 chạy [0,2), B1 chạy [2,5).
Tại t=5, A2 được release với deadline 10, còn B1 có deadline 7.
EDF tiếp tục chọn B1 chạy [5,6), hoàn thành tại t=6 <= 7.

Ví dụ phân biệt EDF/RM: với tập COMPARE gồm A(1,3), B(1,4), C(2,7),
tại t=4, C1 có deadline 7 và B2 có deadline 8; EDF chọn C1.


## 2. OPRTS-38 — Rate Monotonic (RM) và RTA

### Định nghĩa

RM gán ưu tiên cố định theo chu kỳ của task:
T càng nhỏ thì ưu tiên càng cao. Chỉ lựa chọn trong các job sẵn sàng.

Trong simulator của nhóm, thứ tự lựa chọn là:
(period, release_time, task_id).

Với tập COMPARE tại t=4, B2 thuộc task có T=4, còn C1 thuộc task
có T=7. RM chọn B2, dù deadline của C1 sớm hơn.
Vì vậy, D = T không làm EDF và RM trở thành cùng một thuật toán.

Phần phân tích dưới đây sử dụng các giả định đã nêu ở phần EDF.

### Cận Liu–Layland: điều kiện đủ

Với n task, RM được bảo đảm đáp ứng deadline nếu:

$$
U \le n\left(2^{1/n}-1\right)
$$

Đây là điều kiện đủ, không phải điều kiện cần:

- U không vượt cận: có thể kết luận khả thi trong mô hình.
- U vượt cận: chưa thể kết luận thất bại; cần phân tích tiếp bằng RTA.

Ví dụ COMPARE có U = 73/84 ≈ 0.8690, lớn hơn cận cho 3 task
≈ 0.7798, nhưng RTA cho R = (1,2,6), đều không vượt deadline (3,4,7).
Do đó, tập này vẫn được RM chấp nhận.

### Response-Time Analysis (RTA)

Sắp task theo thứ tự ưu tiên RM. Gọi hp(i) là tập task ưu tiên cao hơn i.
Khởi tạo R_i^(0) = C_i và lặp:

$$
R_i^{(k+1)}
=
C_i+\sum_{h\in hp(i)}
\left\lceil\frac{R_i^{(k)}}{T_h}\right\rceil C_h
$$

- Hội tụ tại R_i <= D_i: task i đạt.
- Một giá trị lặp vượt D_i: task i không đạt.
- Toàn bộ task đạt thì tập task được chấp nhận.

Trong simulator: kiểm tra tham số, từ chối nếu U > 1;
nếu U <= 1 thì dùng RTA để quyết định ACCEPT hoặc REJECT_RTA.
Simulator không dùng cận Liu–Layland để từ chối tập task.

### Vì sao U = 34/35 qua EDF nhưng RM thất bại?

Với A(2,5), B(4,7), A có ưu tiên cao hơn B.

Task A: R_A = 2 <= 5, đạt.

Task B:

$$
R_B^{(0)}=4
$$

$$
R_B^{(1)}=4+\left\lceil4/5\right\rceil\times2=6
$$

$$
R_B^{(2)}=4+\left\lceil6/5\right\rceil\times2=8>7
$$

RM từ chối tập task bằng REJECT_RTA.

Nếu bỏ qua admission để chẩn đoán: A1 chạy [0,2), B1 chạy [2,5),
A2 có ưu tiên cao hơn nên chạy [5,7). Tại t=7, B1 còn 1 tick:
ghi nhận một deadline miss và DROP-LATE theo mô hình simulator.

Giá trị 8 là kết quả phân tích RTA vượt deadline, không phải thời điểm
B1 hoàn thành trong trace DROP-LATE: B1 đã bị drop tại t=7.
