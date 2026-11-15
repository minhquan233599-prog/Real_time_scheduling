# Kết quả kiểm tra phần Thành — Tuần 4

## Phạm vi

Kiểm tra bản tích hợp metadata và bộ chọn EDF của Quân, vòng đời job RT của Dũng, cùng trace và bộ kiểm thử của Thành. Bản tích hợp làm việc trên nhánh `feature/week4-thanh-edf_integration`.

Các kết quả dưới đây được cập nhật theo xác nhận của Thành rằng đợt kiểm thử cuối đã PASS trên cả `CPUS=1` và `CPUS=4`. Số liệu chi tiết đối chiếu tại các log cuối trong mục Minh chứng; không dùng số liệu của những đợt chạy cũ để thay thế.

Phần triển khai của Thành:

- Thêm `kernel/rttrace.c` và `kernel/rttrace.h`: lưu sự kiện vào bộ đệm có giới hạn, đánh số bản ghi và đếm bản ghi bị mất.
- Thêm syscall `rt_trace_ctl()` và `rt_trace_read()` để điều khiển và đọc trace từ user-space; nối tại `syscall.h`, `syscall.c`, `user.h` và `usys.pl`.
- Bổ sung điểm ghi release, tick/budget, hết budget, completion, deadline miss và refill trong `kernel/rt.c`.
- Bổ sung trace dispatch RT và tiến trình thường trong `scheduler()` tại `kernel/proc.c`; nhả `rt_lock` trước khi chuyển ngữ cảnh.
- Thêm `user/rt_w4test.c`: kiểm tra vòng đời job, budget, completion, miss/carry-over, điều kiện dispatch và số trang RAM khả dụng trước/sau bộ test.
- Thêm `user/rt_edftest.c`: kiểm tra deadline nhỏ nhất, tie-break theo PID, fallback trên CPU 0, preemption và benchmark.
- Thêm `scripts/check_inodes.py` để đo và so sánh inode trước/sau; thêm `scripts/test_grind.py` để theo dõi tiến độ, phát hiện lỗi và tự kết luận cho `grind`.
- Cập nhật `kernel/defs.h` và `Makefile` để build các thành phần trên.

Chính sách tích hợp: `D = T`, tác vụ RT chạy trên CPU 0. Job chưa hoàn thành khi đến kỳ mới được cấp lại budget nhưng giữ job ID và deadline cũ; mỗi job chỉ được đếm miss một lần. Đây là carry-over, khác chính sách DROP-LATE của simulator.

## Kết quả

| Kiểm tra | CPUS=1 | CPUS=4 |
|---|---|---|
| Build kernel và `fs.img` | Thành công | Dùng cùng bản build |
| `rt_job_smoke` | PASS | PASS |
| Sáu ca `rttest` qua `rtstress` | 6/6 PASS mỗi lượt | 6/6 PASS mỗi lượt |
| `rtstress` | 10/10 PASS | 10/10 PASS |
| `rt_w4test 10` | 10/10 PASS | 10/10 PASS |
| Release, budget, completion, miss và carry-over | PASS | PASS |
| `rt_edftest 10` | 10/10 PASS | 10/10 PASS |
| Deadline nhỏ nhất và tie-break theo PID | PASS | PASS |
| Fallback cho tiến trình thường trên CPU 0 | PASS | PASS |
| Preemption tại điểm lập lịch theo thiết kế | PASS | PASS |
| Kiểm tra trace | PASS, không mất bản ghi | PASS, không mất bản ghi |
| Free pages trước/sau `rt_w4test` | PASS, số trang không giảm | PASS, số trang không giảm |
| Free inodes trước/sau khoảng test đã đo | PASS, số inode không đổi | PASS, số inode không đổi |
| Stock `usertests` | ALL TESTS PASSED | ALL TESTS PASSED |
| `grind` qua script tự đánh giá | PASS | PASS |
| `ls` và `echo` sau suite | Hoạt động | Hoạt động |
| Benchmark `rt_edftest --bench` | PASS, đủ 5 mẫu | PASS, đủ 5 mẫu |

Các chương trình test tự kiểm tra kết quả, in PASS/FAIL và trả mã thoát. Người chạy mở QEMU và gọi lệnh; không cần một script điều khiển toàn bộ suite để thực hiện các bài test này. Riêng `grind` được điều khiển và đánh giá bằng script vì chương trình stock chạy liên tục.

## Benchmark

- **Lệnh:** `rt_edftest --bench`.
- **Chỉ số:** trong mỗi lượt, lấy độ trễ lớn nhất quan sát được từ release của job có deadline sớm hơn đến lần dispatch EDF của job đó trong tình huống preemption.
- **Số lần đo:** 5 lượt cho mỗi cấu hình CPU.
- **Đơn vị:** tick; trace được bật khi đo.
- **Thống kê:** năm mẫu, trung bình, nhỏ nhất, lớn nhất và khoảng biến thiên `max − min`, được lưu trong log cuối.

`BENCH RESULT: PASS` xác nhận thu đủ các lượt đo hợp lệ và các kiểm tra đi kèm đạt; không tự xác nhận một ngưỡng hiệu năng. Giá trị `0 tick`, nếu xuất hiện, nghĩa là release và dispatch được ghi trong cùng tick, không có nghĩa độ trễ bằng 0 giây.

## Các lỗi từng xảy ra

| Lỗi hoặc hiện tượng | Nguyên nhân và cách xử lý |
|---|---|
| Budget, dispatch và carry-over báo FAIL dù có cấp lại budget | Thiếu sự kiện `MISS`/`REFILL` trong trace. Bổ sung điểm ghi sau khi cập nhật metadata, trước khi nhả `rt_lock`; kiểm thử lại. |
| EDF CPUS=4 từng có lượt tie-break `comparisons=0`, tổng kết 9/10 FAIL | Không tạo được tình huống hai job cùng release, chưa có bằng chứng chọn sai PID. Tách kiểm tra một lần và chỉ thử lại tối đa 3 lần khi thiếu tình huống; lỗi worker, trace hoặc chọn sai vẫn FAIL ngay. |
| Preemption có `observations=0`, `lost=0` | Khoảng chạy của workload quá ngắn và phụ thuộc tick lấy mẫu. Điều chỉnh tác vụ deadline xa từ `T=40, C=12`, chạy 12 tick sang `T=80, C=30`, chạy 26 tick; giữ điều kiện phải có quan sát hợp lệ. |
| Benchmark in `mean=0.%02d` | `printf` xv6 không hỗ trợ `%02d`. Đổi sang cách in phần thập phân bằng `%d.%d%d`; giữ phép tính và các mẫu đo. |
| Build lỗi `-Werror=misleading-indentation` trong `check_event()` | Khối bỏ qua `RT_EV_NORMAL` thụt lề gây hiểu nhầm. Căn lại lề, thêm ngoặc và build lại; giữ nguyên `-Werror`. |
| Linker báo `multiple definition` | Object như `proc.o`/`rt.o` bị liệt kê lặp trong `OBJS`. Giữ mỗi object đúng một lần. |
| Linker không tìm thấy hàm trace hoặc `main` | File mới chưa lưu hoặc thiếu nội dung; phần trace cũng cần có `rttrace.o` trong `OBJS`. Lưu đầy đủ file và kiểm tra cấu hình build. |
| VS Code gạch đỏ lời gọi `printk` | IntelliSense từng nhận diện sai trong khi bản đã lưu vẫn build được bằng compiler RISC-V. Giữ khai báo variadic đúng, kiểm tra cấu hình/cache của editor; không đổi code kernel chỉ để bỏ gạch đỏ. |

Những lần FAIL cũ là lịch sử phát triển. Trạng thái nghiệm thu hiện tại trong bảng Kết quả được cập nhật theo xác nhận PASS sau đợt chạy lại cuối. Không sửa nội dung log cũ để biến FAIL thành PASS. Cảnh báo editor không phải kết luận thất bại của bài test thực thi.

## Minh chứng

Các đường dẫn sau tính từ thư mục `docs/week4/`:

- [Log kiểm thử cuối CPUS=1](logs/thanh-test-cpus1-final.log).
- [Log kiểm thử cuối CPUS=4](logs/thanh-test-cpus4-final.log).
- [Log grind CPUS=1](logs/thanh-test_grind-cpus1.log).
- [Log grind CPUS=4](logs/thanh-test_grind-cpus4.log).
- [Kết quả inode trước và sau](logs/thanh-test_inodes-before_after/).

Mã kiểm thử có thể chạy lại tại `user/rt_w4test.c`, `user/rt_edftest.c`, `scripts/check_inodes.py` và `scripts/test_grind.py` trong repository. Khi nộp, gắn các minh chứng với commit hoặc PR của bản tích hợp đã chạy; log từ nhánh tự triển khai trước đó không tự thay thế log của bản ghép mới.

## Giới hạn

- `CPUS=4` kiểm tra hoạt động của hệ thống nhiều CPU theo chính sách RT trên CPU 0; chưa phải triển khai EDF đa lõi.
- EDF order, tie-break và preemption được kiểm tra bằng các tình huống có kiểm soát. Kết quả chưa chứng minh đúng với mọi tập tác vụ hoặc mọi diễn tiến đồng thời.
- Tick vẫn theo baseline, khoảng 0,1 giây. Budget là accounting theo tick lấy mẫu; chưa đo độc lập toàn bộ CPU time hoặc chứng minh mọi trường hợp ngủ/chờ đều được accounting chính xác.
- Đường kernel đang chạy được phép hoàn tất trước khi chặn trở về user. Vì vậy đây chưa phải cơ chế giới hạn CPU cứng hoặc bảo đảm deadline tuyệt đối.
- Đo tài nguyên chỉ có giá trị cho khoảng test được bao quanh bởi phép đo. Không suy rộng kết quả RAM/inode của `rt_w4test` sang tất cả các chương trình chạy ở phiên khác.
- Grind PASS nghĩa là đủ thời gian quan sát theo script, có tiến độ A/B và không phát hiện lỗi theo tiêu chí của script; không chứng minh không bao giờ xảy ra lỗi.
- Trace và workload kiểm thử có ảnh hưởng đến thời gian chạy. Chưa kết luận hiệu năng vượt scheduler gốc hoặc RM chỉ từ các số đo này; chưa đối chiếu đầy đủ toàn bộ lịch chạy với simulator.
