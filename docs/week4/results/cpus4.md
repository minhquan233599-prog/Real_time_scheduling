# TỔNG KẾT MINH CHỨNG TUẦN 4 — CPUS=4

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
Cấu hình thực tế trong lệnh QEMU: -smp 4.  
CPUS=4 không đồng nghĩa EDF đa lõi; tác vụ RT vẫn chạy trên CPU 0.

## Chức năng syscall và vòng đời job

Nguồn syscall: `dung-qemu-cpus4.log` ngày 14/11.  
rt_job_smoke: PASS.  
rttest: 6/6 PASS.  
rtstress: 10/10 PASS.  
Nguồn vòng đời job: `thanh-w4-cpus4.log`, `inode-test-cpus4.log` ngày 15/11.  
rt_w4test 10: 10/10 PASS.  
Các CHECK release, budget transitions, dispatch eligibility and CPU,  
completion, deadline miss and carry-over: PASS.  
Các phiên trace của bộ kiểm tra vòng đời job: lost=0.  
Kết quả syscall ngày 14/11 là đợt tích hợp trước, không xác nhận lại  
toàn bộ mã nguồn EDF cuối cùng bằng chính log này.

## EDF

Nguồn: `edf-cpus4.log`, ngày 15/11, 14:26:42–14:27:05 (+07:00).  
Lệnh được lưu: rt_edftest (một lượt).  
Deadline nhỏ nhất: PASS; 1 phép so sánh.  
Tie-break PID: PASS; 3 phép so sánh.  
Fallback trên CPU 0: PASS; 20 quan sát.  
EDF RESULT: 1/1 PASS; các trace báo lost=0.  
File này chưa có ca preemption và chưa ghi rt_edftest 10.

Preemption có minh chứng riêng trong `benchmark-cpus4.log`:  
5 lần CHECK preemption: PASS; mỗi lần 1 quan sát, delay_ticks=0, lost=0.  
Không cộng năm lượt benchmark thành mười lượt bộ EDF đầy đủ.

Log cũ cần giữ đúng trạng thái:  
`edf-order-10-cpus4.log`: W4 RESULT: 9/10 FAIL; W4 SUITE: FAIL.  
Lượt 3: "missing simultaneous-release scenario", comparisons=0 ở tie-break.  
Đây là thiếu tình huống kiểm tra; không có bằng chứng trong đoạn đó  
rằng scheduler đã chọn sai PID. Chưa có log chạy lại đầy đủ 10/10  
của bộ EDF CPUS=4 trong commit nguồn để thay thế kết quả cũ này.

## Tài nguyên trước và sau

Nguồn: `inode-test-cpus4.log`, `inodes-cpus4-before.json`,  
`inodes-cpus4-after.json`, `inodes-cpus4-result.txt`.  
Bài chạy trong khoảng đo: rt_w4test 10.  

| Tài nguyên | Trước | Sau | Kết quả |
|---|---:|---:|---|
| Free pages | 32380 | 32380 | PASS |
| Free inodes | 172 | 172 | PASS |
  
W4 SUITE: PASS.  
Đây là số liệu của phiên rt_w4test, không bao gồm toàn bộ các bài EDF,  
hồi quy và benchmark được chạy ở các phiên khác.

## Hồi quy

Nguồn usertests: `dung-qemu-cpus4.log` ngày 14/11.  
usertests: PASS — ALL TESTS PASSED.  
ls và echo dung-cpus4-shell-ok: chạy được.  
Không có `regression-cpus4.log` ngày 15/11 trong thư mục log tại commit nguồn.  
Nguồn grind: `grind-cpus4.log` ngày 15/11.  
Phiên QEMU: 11:46:21 đến 11:50:36 (+07:00), tổng 4 phút 15 giây.  
Khoảng này gồm cả khởi động; log không đánh dấu riêng thời điểm bắt đầu grind.  
Grind có tiến độ: A=19, B=19; không thấy thông báo grind lỗi hoặc panic.  
Trước khi vào shell có "ireclaim: orphaned inode 32"; không phải kết luận  
rò rỉ của phiên rt_w4test đo inode riêng.  
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
Nguồn: `benchmark-cpus4.log`, ngày 15/11, 14:38:00–14:38:59 (+07:00).

## Lỗi đã xử lý và giới hạn

MISS/REFILL đã xuất hiện trong các trace vòng đời job có kết quả PASS.  
Một lượt tie-break cũ không tạo được simultaneous-release scenario.  
Cần phân biệt thiếu tình huống với scheduler chọn sai; chưa đánh dấu đã sửa.  
Benchmark còn lỗi hiển thị %02d; số liệu được tổng hợp từ năm mẫu gốc.  
Preemption cho phép hoàn tất đường kernel theo thiết kế hiện tại;  
kết quả không chứng minh giới hạn thời gian thực cứng.

## Lỗi bổ sung và việc chưa hoàn tất

1. Tie-break thiếu tình huống hai job cùng release. Nguồn cũ: `edf-order-10-cpus4.log`, lượt 3, comparisons=0, W4 RESULT: 9/10 FAIL; không có bản ghi chứng minh chọn sai PID. Đã hướng dẫn tách test_edf_order_once và thử lại tối đa 3 lần chỉ khi thiếu tình huống; lỗi chọn sai/trace/worker vẫn FAIL ngay. **CHƯA CÓ LOG** xác nhận bản sửa đạt bộ EDF CPUS=4 đủ 10/10.

2. Preemption thiếu tình huống trong lượt chạy lại. Nguồn: output người chạy gửi trong cuộc trao đổi, EDF RUN 2/10. PREEMPT TRACE: observations=0 lost=0. PREEMPT: missing required scenario; CHECK preemption: FAIL. Không mất trace; chưa quan sát được tình huống để đánh giá chọn EDF. Chưa có bằng chứng từ output này rằng scheduler chọn sai tiến trình. Đã hướng dẫn đổi workload low từ T=40,C=12/chạy 12 tick sang T=80,C=30/chạy 26 tick, giữ nguyên điều kiện PASS. **CHƯA XÁC NHẬN** bản chỉnh sửa chạy thành công.

3. Build báo -Werror=misleading-indentation tại check_event(), `user/rt_w4test.c:77–80`, do if RT_EV_NORMAL thụt lề gây hiểu nhầm. Lỗi xảy ra lúc build chương trình user, trước khi chạy QEMU. Đã hướng dẫn căn lề và thêm ngoặc; **CHƯA XÁC NHẬN** build lại thành công.

4. Benchmark dùng %02d mà printf xv6 không hỗ trợ. Biểu hiện: mean=0.%02d; các đối số in phía sau cũng có thể bị lệch. Năm mẫu gốc vẫn được giữ; thống kê ở trên tính lại từ các mẫu. Đã hướng dẫn đổi cách in sang %d.%d%d; **CHƯA CÓ LOG** mới xác nhận.

5. Grind chưa có kết luận PASS/FAIL tự động. Đã cung cấp `scripts/test_grind.py` để theo dõi 60 giây mỗi cấu hình, kiểm tra A/B, thông báo lỗi và thời gian không có tiến độ. **CHƯA CÓ KẾT QUẢ** chạy script; chưa đánh dấu grind tự động PASS.

6. IntelliSense từng báo printk không tương thích/too many arguments dù các lần build kernel được cung cấp trước đó đã thành công. Chưa xác nhận đã xử lý hết cảnh báo editor; không coi là kernel panic.

## Kết luận

Có minh chứng vòng đời job/stress 10 lượt, RAM/inode, EDF một lượt,  
preemption năm lượt qua benchmark và hồi quy của đợt tích hợp trước.  
Chưa đủ log để ghi bộ EDF cuối cùng CPUS=4 đạt 10/10.  
Grind chưa có kết luận tự động; hồi quy bản EDF cuối chưa được xác nhận.  
Không ghi "tất cả PASS" cho cấu hình này dựa trên commit nguồn hiện tại.  
Output chạy lại còn có FAIL do thiếu tình huống preemption.  
Tất cả hướng dẫn chỉnh sửa mới ở trên đang chờ xác nhận bằng kết quả chạy;  
không đánh dấu đã khắc phục hoặc nâng kết quả thành 10/10 PASS.
