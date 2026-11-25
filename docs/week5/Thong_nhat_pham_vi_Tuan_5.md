# Tuần 5 — Phạm vi và các thống nhất quan trọng

**Dự án:** Real-time Scheduling trên xv6 — Quân, Dũng, Thành  
**Ngày lập:** 18/11/2026  
**Trạng thái:** Bản thống nhất triển khai, không phải báo cáo đã hoàn thành.  
**Vị trí đề xuất trong repo:** `docs/week5/README.md`

## 1. Mục tiêu và giới hạn tuần này

Tích hợp RM cùng tồn tại với EDF trên nền tuần 4, chuyển chính sách an toàn, chạy cùng bộ tác vụ và dùng trace thực nghiệm để giải thích sự khác biệt giữa hai thuật toán.

### Trong phạm vi

- Bộ chọn RM, tie-break ổn định và syscall đọc/đổi policy.
- Workload dùng chung cho EDF/RM; test policy và test RM tách riêng.
- Kiểm tra vòng đời job, budget và hồi quy sau tích hợp.
- Kiểm thử CPUS=1 và CPUS=4; stress/concurrency ít nhất 10 lượt mỗi cấu hình.
- Đo tài nguyên, chạy usertests/grind và benchmark theo yêu cầu chung.
- CSV, Gantt và báo cáo so sánh ngắn, có nguồn dữ liệu rõ ràng.

### Không tự mở rộng trong tuần này

- Không chuyển thành EDF/RM đa lõi; tác vụ RT vẫn ràng buộc CPU 0.
- Không tự thay timer, mô hình job, chính sách carry-over hoặc viết lại toàn bộ scheduler.
- Không triển khai admission RM đầy đủ bằng RTA trong phần việc hiện tại; mục này thuộc kế hoạch tuần sau.
- Không tạo lại bộ test tuần 4 hoặc một hệ thống chạy tự động lớn nếu công cụ hiện có đủ dùng.

Nếu cần thay đổi ngoài phạm vi để xử lý một lỗi thực tế, cả nhóm thống nhất tác động trước khi sửa. Không tự đổi đầu vào hoặc điều kiện PASS để phù hợp với kết quả đang có.

## 2. Mô hình chạy và thứ tự ưu tiên

| Nội dung | Thống nhất |
|---|---|
| Mô hình RT | Tác vụ tuần hoàn, deadline tương đối D=T |
| Đơn vị | C, T, release và deadline tính bằng tick |
| CPU | RT chạy CPU 0; CPU khác chạy tiến trình thường |
| CPUS=4 | Kiểm tra bản tích hợp trong môi trường 4 CPU; không gọi là lập lịch RT đa lõi |
| EDF | Chọn job đủ điều kiện có deadline tuyệt đối nhỏ nhất |
| RM | Chọn tác vụ đủ điều kiện có T nhỏ nhất |
| Tie-break | Bằng deadline với EDF hoặc bằng T với RM thì chọn PID nhỏ hơn |
| Fallback | CPU 0 chạy tiến trình thường khi không có RT đủ điều kiện và không có đường kernel cần ưu tiên |
| Đường kernel | Giữ cơ chế cho tiến trình bị kill/kernel continuation hoàn tất như tuần 4 |

“Đủ điều kiện” gồm process RUNNABLE, đúng CPU, đăng ký hợp lệ, job READY, đã đến release và remaining > 0. RUNNABLE không đồng nghĩa được phép chạy workload RT.

EDF và RM phải giữ được logic ưu tiên riêng. Có thể đặt hàm chọn/so sánh riêng trong `kernel/proc.c` và dùng chung phần quét, khóa, kiểm tra ứng viên; không cần tạo file kernel mới chỉ để đổi tiêu chí. Tài liệu Sprint 5 nêu `rm_pick` riêng: khi chốt code cuối, Quân cần bảo đảm phần RM được nhận diện rõ, không ghi đè hoặc làm mất logic EDF.

## 3. Giao diện policy — dùng chung giữa ba người

Header: `kernel/schedpolicy.h`.

```c
#define SCHED_EDF 0
#define SCHED_RM  1

// Đọc chính sách toàn hệ thống.
int sched_getpolicy(void);

// Đặt chính sách toàn hệ thống theo hợp đồng bên dưới.
int sched_setpolicy(int policy);
```

| Tình huống | Kết quả |
|---|---|
| Khởi động mới | EDF |
| getpolicy | Trả SCHED_EDF hoặc SCHED_RM |
| setpolicy với giá trị khác 0/1 | -1; không thay chính sách |
| Đặt lại cùng policy | 0; không thay metadata, job hoặc budget |
| Đổi sang policy khác, không có đăng ký RT | 0 |
| Đổi sang policy khác khi có enabled/active/pinned RT | -2; không thay chính sách |

- Đây là policy toàn hệ thống, không phải policy riêng từng process.
- Bản hiện tại chặn đổi ngay cả khi RT đang ngủ, đợi release hoặc tạm hết budget; không chỉ kiểm tra job đang chạy.
- Kiểm tra điều kiện và đổi policy trong cùng vùng `rt_lock`.
- Bộ chọn phải nhận biết policy thay đổi trong lúc quét và quét lại nếu cần.
- Syscall dự kiến: setpolicy=27, getpolicy=28; Quân kiểm tra không trùng khi tích hợp.
- Quân quản lý các khai báo syscall. Dũng và Thành dùng đúng API này, không tự đổi tên, số hoặc mã trả về.

## 4. Các nguyên tắc kernel phải giữ

- Tất cả trường `p->rt`, đồng hồ `rt_now` và trạng thái policy dùng `rt_lock`.
- Khi cần cả hai khóa: **p->lock trước, rt_lock sau**. Không lấy p->lock khi đã giữ rt_lock.
- Không giữ rt_lock qua `sched()`, `yield()`, `swtch()` hoặc thao tác có thể ngủ.
- `rt_pick_next()` trả tiến trình với p->lock đang giữ; không giữ rt_lock khi trả về.
- Sau `swtch()` trong scheduler phải giữ đoạn phục hồi hiện tại: đặt intena=0, c->proc=0 và nhả p->lock.
- CPU 0 cập nhật đồng hồ RT; gọi phần RT sau khi nhả tickslock.
- Kernel continuation được hoàn tất trước khi chặn trở về user. Budget hiện chưa phải giới hạn CPU cứng.

Vòng đời giữ nguyên tuần 4: release tạo job mới; job_done đánh dấu hoàn thành và chờ kỳ tiếp theo; hết budget thì throttled. Job chưa xong khi sang kỳ được refill nhưng giữ job_id, release và deadline cũ; mỗi job chỉ đếm miss một lần.

Gọi lại sched_setdeadline cùng T/C khi active không reset job/budget; đổi cấu hình active vẫn trả -3. Fork không tự kế thừa đăng ký RT; exit thu hồi metadata và tải đăng ký.

## 5. Bộ dữ liệu và ý nghĩa phép so sánh

Bảng ghi theo thứ tự **(C,T)**. Syscall vẫn gọi **sched_setdeadline(T,C)**.

| Bộ | A | B | C | Tổng tải |
|---|---|---|---|---|
| TEST-SAFE | (1,4) | (2,5) | (1,10) | 0,75 |
| TEST-COMPARE | (1,3) | (1,4) | (2,7) | 73/84 ≈ 0,8690 |

Trong mô hình lý tưởng TEST-COMPARE, tại t=4: EDF chọn C1, RM chọn B2; cả hai vẫn không miss. Trên kernel, phải đối chiếu với release thực tế và độ phân giải tick đã công bố.

Khi đổi EDF ↔ RM, giữ cùng C/T, số job, khối lượng công việc, cách khởi động workload và cấu hình đo. Mỗi cặp so sánh có cùng CPUS và cùng bản code, chỉ thay policy. PID có thể khác; dùng ánh xạ PID → A/B/C trong log.

### Những điểm chưa được chứng minh bằng workload hiện tại

- C là budget đăng ký; vòng lặp tính toán không tự tương đương chính xác C tick.
- Tham số work trong rt_compare là lượng tính toán, không phải thời gian CPU.
- Pipe giúp phối hợp khởi động nhưng không bảo đảm ba tác vụ release cùng tick.
- WORKLOAD PASS chưa chứng minh thứ tự EDF/RM hoặc đúng kịch bản t=4.
- Nếu thiếu tình huống so sánh, ghi thiếu coverage và tìm nguyên nhân; không ghi PASS cho yêu cầu chưa được quan sát.

Hai bảng đầu vào hiện được biên dịch trong rt_compare; input_hash nhận diện cấu hình, không phải hash bảo mật. Tài liệu Sprint 5 còn nêu demo cùng input file: phần đọc file đầu vào chưa có trong bản hướng dẫn hiện tại. Nếu nghiệm thu theo mục này, Dũng bổ sung việc đọc một cấu hình chung; không đánh dấu đã hoàn tất khi mới có bảng hard-code.

Admission hiện tại Σ(C/T)≤1 chưa bảo đảm RM khả thi. TEST-COMPARE được RTA lý thuyết xác nhận khả thi, nhưng điều đó không thay thế test kernel, accounting và khóa.

## 6. Chia file và trách nhiệm

| Người | Tạo mới | Chỉnh sửa chính |
|---|---|---|
| Quân | kernel/schedpolicy.h | proc.c, defs.h, sysproc.c, syscall.h/c, user.h, usys.pl; thêm mã trace RM trong rttrace.h và ghi DISPATCH đúng policy |
| Dũng | user/rt_compare.c; phụ trách user/rt_policytest.c đã có mẫu | Rà soát rt.c, chỉ sửa khi phát hiện lỗi vòng đời |
| Thành | user/rt_rmtest.c, scripts/trace_to_csv.py, scripts/plot_rt_gantt.py | Makefile khi tích hợp; các test/script tài nguyên hiện có chỉ sửa phần cần thiết |

- rt_edftest chỉ kiểm tra EDF; rt_rmtest kiểm tra RM; rt_policytest kiểm tra giao diện policy; rt_compare tạo workload so sánh.
- rt_w4test tiếp tục kiểm tra vòng đời. Nếu chạy dưới RM, checker phải hiểu lớp trace RM; không chạy checker chỉ nhận EDF rồi quy FAIL thành lỗi scheduler.
- Một file rt_policytest dùng chung; không tạo bản Quân và bản Dũng khác nhau.
- Thành phụ trách Makefile tích hợp. Mỗi người có thể thêm target cần thiết để tự test trên nhánh, rồi đối chiếu khi merge, tránh mục trùng.
- Không dồn mọi chức năng vào một chương trình test; đồng thời tránh chép nguyên bộ test khi chỉ khác tiêu chí ưu tiên.

## 7. Trace và điều kiện đánh giá

| Nội dung | Quy ước |
|---|---|
| Lớp DISPATCH | KERNEL=0, EDF=1, RM=2 |
| Trace | Dùng bộ đệm hiện có; ghi sau khi cập nhật metadata, khi giữ rt_lock |
| Xuất dữ liệu | STOP trace rồi mới đọc/in CSV |
| lost | Phải bằng 0 để dùng trace làm minh chứng đầy đủ |
| Nguồn | Ghi rõ XV6 hoặc SIMULATOR; không trình bày Gantt mô phỏng như dữ liệu kernel |
| Thông tin phiên | Policy, CPUS, commit/thay đổi chưa commit, cấu hình timer, input_hash và tham số workload |
| 0 tick | Cùng tick quan sát; không có nghĩa độ trễ vật lý bằng 0 |

Test phải tự kiểm tra kết quả và in PASS/FAIL. Có thể mở QEMU và nhập lệnh bằng tay; không bắt buộc xây một script điều khiển toàn bộ nếu test đã tự đánh giá. Log chỉ là minh chứng, không thay cho assert. Thiếu kết quả cuối, treo, timeout hoặc mất trace không được tính PASS.

## 8. Nghiệm thu chung — làm đủ, không nhân bản

| Hạng mục | Điều kiện |
|---|---|
| Build và format | Build thành công; format phù hợp CI |
| Policy | Đọc/đổi đúng, lỗi đầu vào, bận, đặt lại cùng policy, đổi sau khi RT thoát |
| RM | T nhỏ hơn được ưu tiên; cùng T theo PID; preemption và fallback đúng |
| EDF và vòng đời | Chạy hồi quy; kiểm tra release/budget/completion/miss/carry-over theo phạm vi suite |
| Stress/concurrency | Tối thiểu 10 lượt ở CPUS=1 và CPUS=4; ghi rõ policy đã chạy |
| Tài nguyên | Free pages và free inodes trước/sau suite với phạm vi đo xác định |
| Hồi quy | Stock usertests và grind; ghi rõ policy/cấu hình; cả hai mode có smoke và hồi quy non-RT |
| Benchmark | Ít nhất 5 mẫu cho mỗi điểm đo, có trung bình và độ phân tán; ghi rõ metric |
| So sánh | Tối thiểu 2 CSV + 2 Gantt EDF/RM của cùng bộ đầu vào; giải thích khác biệt bằng dữ liệu |

Không yêu cầu mọi test nhỏ đều lặp 10 lần; mức 10 lượt áp dụng stress/concurrency. Không bắt mỗi thành viên chạy lại toàn bộ nghiệm thu nhóm. Dũng/Quân tự kiểm tra phần mình, Thành tổng hợp trên bản tích hợp cuối.

Free pages phải đo sau khi thu hồi các con. Free inodes đo khi QEMU đã dừng, trên cùng fs.img trước/sau; không rebuild image giữa hai lần rồi kết luận không rò rỉ. Nếu chỉ đo quanh rt_w4test, báo cáo phải ghi đúng phạm vi đó, chưa phải tài nguyên toàn suite.

Benchmark phải công bố metric đo. PASS của một lần thu mẫu không tự chứng minh hiệu năng tốt hơn; không tự suy từ số lần PASS thành thời gian thực thi hoặc độ trễ.

## 9. Thứ tự thực hiện và Git

1. Cả nhóm lấy cùng commit nền tuần 4 đã PASS từ main, tạo nhánh riêng.
2. Quân hoàn thành policy/RM và kiểm tra giao diện; Dũng, Thành chuẩn bị theo API thống nhất.
3. Tích hợp workload Dũng, chạy thử cả EDF/RM, kiểm tra trace và độ lệch release.
4. Thành hoàn thành test RM, chuyển CSV và Gantt; nhóm xử lý các tình huống còn thiếu coverage.
5. Chạy nghiệm thu trên bản tích hợp ổn định, lưu log cuối rồi review/PR vào main.

Ví dụ tạo nhánh từ main mới nhất, khi working tree sạch và tên nhánh chưa tồn tại:

```bash
git fetch origin
git switch -c test/week5-integration origin/main
```

Không tạo lại nhánh đã có bằng -c. Không reset/chép đè toàn bộ kernel của nhau để giải quyết conflict; đối chiếu thay đổi theo file. Không dùng log từ binary cũ sau khi build mới thất bại.

## 10. Minh chứng và báo cáo

```text
docs/week5/
  README.md
  logs/
  results/
```

Lưu log build, phiên bản và các phiên chạy cuối theo CPU; không cần một log riêng cho từng ca nhỏ. CSV/Gantt và tổng kết đặt trong results. Một log có thể chứa nhiều test nếu nhận diện rõ lệnh, policy và kết quả.

Báo cáo phải phân biệt: đã triển khai, đã chạy PASS, chưa kiểm tra và giới hạn còn lại. Các hướng dẫn Quân/Dũng hiện là code đề xuất; chưa có xác nhận QEMU PASS cho tuần 5 trong phiên lập tài liệu này.

### Điểm phải chốt trước khi ký hoàn thành

- [ ] Giao diện policy và trace thống nhất giữa ba phần.
- [ ] Logic RM được tách rõ, EDF vẫn hoạt động.
- [ ] Workload đủ dài và release đủ rõ để quan sát trường hợp so sánh; không chỉ có WORKLOAD PASS.
- [ ] Đã xử lý yêu cầu input file chung theo kế hoạch Sprint 5.
- [ ] Các test, tài nguyên, hồi quy và benchmark có kết quả thực tế đúng phạm vi.
- [ ] CSV/Gantt có nguồn, policy và đầu vào tương ứng; báo cáo mô tả đúng giới hạn CPU 0 và tick.

## Tài liệu đối chiếu

- Repo: https://github.com/minhquan233599-prog/Real_time_scheduling
- “Real-time Scheduling EDF/RM — Giáo trình và 8 Sprint”: trang 10 (bộ tác vụ), trang 30 (Sprint 5).
- Huong_dan_Quan_Tuan_5.md: giao diện policy, scheduler, trace và policytest.
- Huong_dan_Dung_Tuan_5.md: workload, dữ liệu đầu vào và giới hạn đo.

Tài liệu này tập trung thống nhất phạm vi, không thêm một bộ code thứ ba thay thế hai hướng dẫn đã có.
