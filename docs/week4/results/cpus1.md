# TỔNG KẾT MINH CHỨNG TUẦN 4 — CPUS=1

Nguồn được đối chiếu tại commit [729caa0](https://github.com/minhquan233599-prog/Real_time_scheduling/commit/729caa01fdf74d9fc750c3a4e67c80ce38424026)

Cập nhật ghi nhận lỗi ngày 15/11/2026 từ trao đổi sau khi đọc commit.  
Các kết quả PASS dưới đây là kết quả lịch sử trong log nguồn.  
Chưa có log mới xác nhận những chỉnh sửa bổ sung đã thành công.

## Phiên bản code và môi trường

Commit chứa minh chứng: 729caa01fdf74d9fc750c3a4e67c80ce38424026  
Repository: https://github.com/minhquan233599-prog/Real_time_scheduling  
Nhánh ghi trong `thanh-version.txt`: test/week4-integration  
Commit ghi trong `thanh-version.txt`: 7955f657c3358286b763fc0ebfeac2cafe9353d4  
Bản ghi phiên bản lúc 11:10 ngày 15/11 chỉ liệt kê hai file log mới chưa commit.  
Không có bản ghi commit/trạng thái working tree riêng cho mọi lần chạy sau.  
Vì vậy, đây là tổng hợp nhiều đợt chạy, không xác nhận tất cả chạy cùng commit.  
Phiên bản QEMU: không được ghi trong các log đã đọc.  
QEMU: qemu-system-riscv64, machine virt, RAM 128 MiB.  
Chính sách kiểm tra: tác vụ RT chạy trên CPU 0; trace bật khi kiểm tra EDF.  
Đường dẫn log nguồn bên dưới tính từ docs/week4/logs/.  
Cấu hình thực tế trong lệnh QEMU: -smp 1.

## Chức năng syscall và vòng đời job

Nguồn: `regression-cpus1.log`, `thanh-w4-cpus1.log`, `inode-test-cpus1.log`.  
rt_job_smoke: PASS.  
Đã có kiểm tra chưa đăng ký, T/C âm, đăng ký hợp lệ, cùng tham số,  
đổi cấu hình đang active trả -3 và rt_job_done qua ba kỳ.  
rttest: 6/6 PASS.  
rtstress: 10/10 PASS.  
rt_w4test 10: 10/10 PASS.  
Các CHECK release, budget transitions, dispatch eligibility and CPU,  
completion, deadline miss and carry-over: PASS.  
Các phiên trace của bộ kiểm tra vòng đời job: lost=0.  
Đây là kiểm tra theo workload/trace hiện có, chưa đo độc lập toàn bộ CPU time.

## EDF

Nguồn: `edf-cpus1.log`, chạy ngày 15/11 từ 14:32:41 đến 14:35:24 (+07:00).  
Lệnh: rt_edftest 10.  
Deadline nhỏ nhất: PASS trong 10 lượt, mỗi lượt có 1 phép so sánh.  
Tie-break PID: PASS trong 10 lượt, mỗi lượt có 3 phép so sánh.  
Fallback trên CPU 0: PASS trong 10 lượt, mỗi lượt 23 quan sát.  
Preemption: PASS trong 10 lượt, mỗi lượt 1 quan sát, delay_ticks=0.  
EDF RESULT: 10/10 PASS.  
Tất cả các dòng ORDER/FALLBACK/PREEMPT TRACE trong file báo lost=0.

## Tài nguyên trước và sau

Nguồn: `inode-test-cpus1.log`, `inodes-cpus1-before.json`,  
`inodes-cpus1-after.json`, `inodes-cpus1-result.txt`.  
Bài chạy trong khoảng đo: rt_w4test 10.  

| Tài nguyên | Trước | Sau | Kết quả |
|---|---:|---:|---|
| Free pages | 32380 | 32380 | PASS |
| Free inodes | 172 | 172 | PASS |
  
W4 SUITE: PASS.  
Đây là số liệu của phiên đo rt_w4test, không bao gồm toàn bộ các bài EDF,  
hồi quy và benchmark được chạy ở các phiên khác.

## Hồi quy

Nguồn usertests: `regression-cpus1.log` ngày 15/11.  
usertests: PASS — ALL TESTS PASSED.  
ls và echo regression-cpus1-shell-ok: chạy được.  
Nguồn grind: `grind-cpus1.log`.  
Phiên QEMU: 11:42:31 đến 11:46:05 (+07:00), tổng 3 phút 34 giây.  
Khoảng này gồm cả khởi động; log không đánh dấu riêng thời điểm bắt đầu grind.  
Grind có tiến độ: A=15, B=15; không thấy thông báo grind lỗi hoặc panic.  
Grind được chủ động dừng bằng việc thoát QEMU.  
Trạng thái grind: đã chạy quan sát; chưa có kết luận PASS/FAIL tự động.  
COMMAND_EXIT_CODE=0 của QEMU không được dùng để kết luận grind PASS.

## Benchmark

Lệnh: rt_edftest --bench  
Chỉ số: mỗi lượt lấy độ trễ lớn nhất quan sát được từ release của job ưu tiên cao  
đến dispatch EDF của nó trong tình huống preemption.  
Đơn vị: tick. Số lượt: 5.  
Các mẫu: 0, 0, 0, 0, 0.  
Mean: 0 tick. Min: 0 tick. Max: 0 tick. Range: 0 tick.  
Các thống kê trên tính lại từ năm dòng BENCH SAMPLE.  
Log gốc in sai phần thập phân: "mean=0.%02d"; không sửa log gốc.  
BENCH RESULT: PASS — đủ năm lượt có kết quả preemption PASS và lost=0.  
0 tick nghĩa là cùng tick, không có nghĩa độ trễ bằng 0 giây.  
Nguồn: `benchmark-cpus1.log`, ngày 15/11, 14:37:24–14:37:58 (+07:00).

## Lỗi đã xử lý và giới hạn

Thiếu trace MISS/REFILL từng làm checker budget/dispatch/carry-over báo FAIL.  
Log còn lưu: `thanh-w4-smoke-cpus1.log` (FAIL), `thanh-w4-debug-cpus1.log` (PASS).  
Các log vòng đời job sau đó có đầy đủ MISS/REFILL và kết quả PASS.  
Benchmark còn lỗi hiển thị %02d; số liệu được tổng hợp từ năm mẫu gốc.  
Preemption cho phép hoàn tất đường kernel theo thiết kế hiện tại;  
kết quả không chứng minh giới hạn thời gian thực cứng.

## Lỗi bổ sung và việc chưa hoàn tất

1. Benchmark dùng %02d mà printf xv6 không hỗ trợ. Biểu hiện: mean=0.%02d; các đối số in phía sau cũng có thể bị lệch. Phép đo năm mẫu vẫn được giữ; thống kê ở trên tính từ các mẫu. Đã hướng dẫn đổi cách in sang %d.%d%d; **CHƯA XÁC NHẬN** sửa thành công.

2. Build báo -Werror=misleading-indentation tại check_event(), `user/rt_w4test.c:77–80`, do if RT_EV_NORMAL thụt lề gây hiểu nhầm. Đây là lỗi build chương trình user, không phải kết quả test scheduler. Đã hướng dẫn căn lề và thêm ngoặc; **CHƯA XÁC NHẬN** build lại thành công.

3. Grind mới có log quan sát, chưa tự kết luận PASS/FAIL. Đã cung cấp `scripts/test_grind.py` để chạy 60 giây mỗi cấu hình, kiểm tra tiến độ A/B, lỗi và thời gian không có tiến độ. **CHƯA CÓ KẾT QUẢ** chạy script; chưa đánh dấu grind tự động PASS.

4. IntelliSense từng báo printk không tương thích/too many arguments dù các lần build kernel được cung cấp trước đó đã thành công. Đây là chẩn đoán của editor; chưa ghi nhận đã xử lý hết cảnh báo.

5. Lỗi thiếu tình huống tie-break và preemption được ghi nhận ở CPUS=4 được mô tả trong `cpus4.md`; không chuyển thành FAIL của CPUS=1. Nếu dùng mã test đã chỉnh sửa chung, cần xác nhận lại kết quả bản mới.

## Kết luận

Đã có minh chứng PASS cho syscall, stress 10 lượt, vòng đời job, EDF 10 lượt,  
free pages/inodes của phiên rt_w4test, usertests và năm lượt benchmark.  
Grind đã có log tiến độ nhưng chưa tự đánh giá PASS/FAIL.  
Chưa coi đây là một đợt nghiệm thu toàn bộ trên một phiên bản nguồn duy nhất.  
Các lỗi bổ sung ở trên đang để trạng thái chưa xác nhận sửa thành công;  
không sử dụng PASS của log cũ để kết luận bản chỉnh sửa mới đã đạt.
