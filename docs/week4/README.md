# Tuần 4 — Tích hợp EDF vào kernel xv6

## 1. Mục tiêu

Triển khai Earliest Deadline First (EDF) trong kernel xv6:

- Quản lý release, deadline, ngân sách CPU và completion của job.
- Chọn job đủ điều kiện có deadline tuyệt đối gần nhất.
- Hỗ trợ preemption tại các điểm lập lịch đã thống nhất.
- Duy trì khả năng chạy tiến trình thường.
- Ghi trace từ kernel và đối chiếu với simulator.
- Kiểm thử trên cả CPUS=1 và CPUS=4.

Tuần 3 đã có metadata đăng ký RT, syscall sched_setdeadline(),
admission tối thiểu và bộ kiểm thử rttest/rtstress.
Các chức năng EDF trong tài liệu này cần được triển khai và nghiệm thu.

## 2. Phân công

| Thành viên | Trách nhiệm |
|---|---|
| Trịnh Minh Quân | Metadata chung, bộ chọn EDF, tích hợp scheduler và chính sách CPU |
| Nguyễn Mạnh Dũng | Release, runtime accounting, budget, rt_job_done() và vòng đời job |
| Bùi Công Thành | Module trace kernel, workload, test tích hợp, đối chiếu và đo lường |
| Cả nhóm | Chốt thiết kế, kiểm thử phần mình, review chéo và cập nhật tài liệu |

Review: Quân review Dũng → Dũng review Thành → Thành review Quân.

## 3. Quy ước và quyết định thiết kế

### Quy ước kế thừa

- API đăng ký: sched_setdeadline(period, runtime), tức T trước, C sau.
- Deadline tương đối D = T.
- Đơn vị thời gian là tick; không mặc định một tick bằng 1 ms.
- Deadline tuyệt đối: d = release + T.
- Admission hiện tại kiểm tra tổng tải Σ(C/T) ≤ 1.
- Điều kiện này gắn với mô hình EDF một CPU, chưa bảo đảm RM.

### Các nội dung phải chốt trước khi tích hợp

| Nội dung | Quyết định cần ghi trong design.md |
|---|---|
| Release đầu tiên | Ngay khi đăng ký hay tại một mốc bắt đầu chung |
| Chu kỳ tiếp theo | Release trước + T; xử lý trường hợp đã lỡ nhiều kỳ |
| Completion | Hợp đồng rt_job_done(), trạng thái hợp lệ và mã lỗi |
| Ngân sách | Cách đo CPU time, phạm vi tính user/kernel, xử lý tick biên |
| Hết ngân sách | Thời điểm ngừng phục vụ và điều kiện được chạy lại |
| Job chưa xong tại deadline | Chính sách xử lý và cách phối hợp với user program |
| EDF tie-break | Thứ tự khi hai job có cùng deadline |
| Preemption | Sự kiện yêu cầu lập lịch lại và điểm chuyển an toàn |
| Đăng ký lại | Hành vi khi tiến trình đang có job hoạt động |
| Lifecycle | Quy tắc fork, exit, kill và exec |
| Khóa | Khóa bảo vệ từng trường, thứ tự khóa, điều kiện gọi helper |
| Trace | Trường dữ liệu, sự kiện, lấy dữ liệu và xử lý đầy buffer |

Release là thời điểm job được phép bắt đầu chạy, không phải thời điểm
job chắc chắn được cấp CPU.

Hết budget không tự đồng nghĩa job đã hoàn thành công việc.
Nếu chọn DROP-LATE, phải có giao thức bỏ công việc cũ thực sự;
chỉ reset metadata là chưa đủ.

## 4. Chính sách CPU

Phải kiểm thử với cả CPUS=1 và CPUS=4.

Phương án đề xuất cần nhóm xác nhận:

- Tác vụ RT chạy EDF trên CPU 0.
- Khi CPUS=4, các CPU còn lại chạy tiến trình thường.
- CPU 0 có thể phục vụ tiến trình thường khi không có RT đủ điều kiện.
- Tiến trình đăng ký RT từ CPU khác phải chuyển sang CPU 0 an toàn.
- Không tính ngân sách hoặc release trùng từ nhiều CPU.

Đây là lựa chọn thiết kế của nhóm, không phải yêu cầu thầy chỉ định.
Nếu chọn phương án khác, phải cập nhật đặc tả, admission và bộ test.

## 5. Thứ tự triển khai

1. Xác nhận baseline Tuần 3 build và test được.
2. Chốt design.md và giao diện helper dùng chung.
3. Bổ sung metadata, release, budget và completion.
4. Tích hợp EDF picker, scheduler và preemption.
5. Hoàn thiện chính sách CPU và đồng bộ trên CPUS=4.
6. Tích hợp trace kernel và chương trình kiểm thử.
7. Chạy regression, đo tài nguyên và đo lường.
8. Review, lưu minh chứng và cập nhật kết quả thực tế.

Mỗi bước phải build và kiểm tra trước khi chuyển sang bước tiếp theo.

## 6. Build và khởi động

Chạy tại thư mục gốc repository trong terminal Ubuntu:

```bash
mkdir -p docs/week4/logs

git branch --show-current
git rev-parse HEAD
git status --short

make clean
make kernel/kernel fs.img > docs/week4/logs/build.log 2>&1
echo $?
```

Mã thoát build phải bằng 0. Nếu lỗi:

```bash
tail -n 30 docs/week4/logs/build.log
```

Chạy và ghi log cấu hình một CPU:

```bash
script -q -e -c 'make qemu CPUS=1' \
  docs/week4/logs/qemu-cpus1.log
```

Thoát QEMU bằng Ctrl+A, thả phím rồi nhấn X.
Sau đó có thể chạy cấu hình bốn CPU:

```bash
script -q -e -c 'make qemu CPUS=4' \
  docs/week4/logs/qemu-cpus4.log
```

Chỉ mở một phiên QEMU dùng fs.img tại một thời điểm.
Các lệnh script trên ghi đè log cùng tên; lưu bản cũ nếu cần.

## 7. Kiểm thử

### Kiểm tra baseline Tuần 3

Trong shell xv6:

```text
rttest
rtstress
ls
echo shell-ok
```

Kết quả mong đợi của bộ RT hiện tại:

```text
RESULT: 6/6 PASS
RT STRESS RESULT: 10/10 PASS
```

Sau khi mở rộng vòng đời job, phải kiểm tra tính tương thích với các
test cũ. Nếu hợp đồng API thay đổi, ghi rõ lý do điều chỉnh test.

### Bộ test Tuần 4 cần triển khai

| Nhóm | Nội dung |
|---|---|
| EDF | Deadline gần nhất, tie-break và điều kiện được chọn |
| Release | Job chưa release không được chọn; kỳ tiếp theo đúng mốc |
| Completion | Job hoàn thành ngừng được chọn và chờ kỳ tiếp theo |
| Budget | Chỉ tính khi chạy, không tính khi ngủ, xử lý hết budget |
| Preemption | Job ưu tiên cao hơn được phục vụ theo hợp đồng lập lịch |
| CPU | Đúng chính sách CPU, không chạy cùng tiến trình trên hai CPU |
| Fallback | Tiến trình thường hoạt động khi không có RT đủ điều kiện |
| Lifecycle | Fork, exit, đăng ký lại và đường xử lý lỗi |
| Stress | Ít nhất 10 lượt trên mỗi cấu hình CPUS=1 và CPUS=4 |
| Tài nguyên | Free pages và free inodes trước/sau suite |
| Hồi quy | Stock usertests và grind |

Mỗi test phải tự kiểm tra và in PASS/FAIL.
Cần có một lệnh chạy suite tổng hợp; lệnh chính thức sẽ được bổ sung
vào đây sau khi chương trình điều khiển được triển khai.

Không coi mã thoát QEMU bằng 0 là kết quả kiểm thử.
Với grind, phải quy định thời lượng, kiểm tra tiến triển/lỗi và
tiêu chí kết luận rõ ràng.

Khi đo inode, dùng cùng fs.img trước/sau; không build lại ảnh đĩa
giữa hai lần đo và chỉ đọc ảnh khi QEMU đã dừng.

## 8. Trace và đo lường

Trace dự kiến có:

- Timestamp và đơn vị thời gian.
- CPU ID, PID và job ID.
- Release time, deadline và budget.
- Sự kiện release, dispatch, preempt, complete và hết budget.

Quy tắc:

- Gắn nhãn nguồn XV6 hoặc SIMULATOR.
- Dùng buffer có giới hạn; báo số bản ghi bị mất khi đầy.
- Tránh printf liên tục trong timer/scheduler.
- Trace bị mất bản ghi không được dùng để kết luận toàn bộ lịch chạy đúng.
- Đối chiếu theo mốc thời gian và độ phân giải đã công bố.
- Ghi nhận, giải thích sai khác; không sửa oracle chỉ để khớp kernel.

Chọn ít nhất một chỉ số đo có định nghĩa rõ, ví dụ response time
từ release đến completion của job.

Mỗi điểm đo chạy ít nhất 5 lần; báo trung bình và độ phân tán,
ghi rõ workload, cấu hình CPU, đơn vị, cách thu thập và ảnh hưởng
của trace. Không diễn giải tick thành ms nếu chưa xác định thời lượng tick.

## 9. Tài liệu và minh chứng

Tài liệu dự kiến trong docs/week4/:

| Đường dẫn | Nội dung |
|---|---|
| README.md | Hướng dẫn build, test và phạm vi hiện tại |
| design.md | Hợp đồng job, scheduler, CPU và khóa |
| test-plan.md | Ca kiểm thử, đầu vào và kết quả mong đợi |
| results.md | Kết quả thực tế, số đo, lỗi và hạn chế |
| logs/ | Log build, QEMU, stress, tài nguyên và hồi quy |
| traces/ | Trace/CSV và kết quả đối chiếu |
| diagrams/ | Sơ đồ xuất từ FigJam |

Tên file có thể điều chỉnh; phải cập nhật lại đường dẫn trong README.

Mỗi đợt nghiệm thu cần ghi:

- Commit hash và thay đổi chưa commit nếu có.
- Ngày chạy, người chạy, cấu hình CPU và phiên bản công cụ.
- Kết quả PASS/FAIL thực tế và đường dẫn log.
- Các lỗi còn mở và giới hạn của phép đo.

## 10. Điều kiện hoàn thành Tuần 4

- [ ] Ba thành viên có đóng góp kernel và review chéo.
- [ ] Hợp đồng job, khóa và chính sách CPU đã được chốt.
- [ ] EDF chọn đúng job đủ điều kiện theo deadline tuyệt đối.
- [ ] Release/completion, budget và preemption cơ bản được kiểm chứng.
- [ ] Có trace kernel và chương trình tự kiểm tra kết quả.
- [ ] Có suite chạy bằng một lệnh.
- [ ] Có stress 10 lượt trên cả CPUS=1 và CPUS=4.
- [ ] Có kiểm tra tài nguyên và hồi quy.
- [ ] Có số đo lặp cùng trung bình và độ phân tán.
- [ ] Boot, shell và các chức năng thường tiếp tục hoạt động.
- [ ] Jira, FigJam và báo cáo phản ánh kết quả thực tế.

Chỉ đánh dấu hoàn thành khi có minh chứng.