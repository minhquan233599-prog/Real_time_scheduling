# Kết quả kiểm tra phần Dũng — Tuần 5

## Phạm vi

Kiểm tra phần workload của Dũng: hiệu chuẩn tự động, tạo workload
giống nhau dưới EDF và RM, xuất trace dạng CSV.

Thay đổi:
- user/rt_calibrate.c: hàm run_blocks và calibrate.
- user/rt_compare.c: SAFE và COMPARE, đồng bộ worker, chạy EDF rồi RM,
  kiểm tra worker, xuất CSV, khôi phục policy ban đầu.
- Makefile: rule liên kết _rt_compare và mục trong UPROGS.

Không thay đổi kernel, syscall hoặc timer. Phần phân tích trace,
vòng đời job, thứ tự EDF/RM và Gantt thuộc Thành.

## Kết quả

| Kiểm tra | CPUS=1 | CPUS=4 |
|---|---|---|
| Build | BUILD_EXIT=0 | Dùng cùng bản build |
| rt_policytest 10 | 10/10 PASS | 10/10 PASS |
| rt_job_smoke | PASS | PASS |
| rttest | 6/6 PASS | 6/6 PASS |
| Calibration | PASS (2 lần) | PASS (3 lần) |
| rt_compare: worker, xuất CSV, khôi phục policy | 2 lần chạy, đều PASS | 3 lần chạy, đều PASS |
| Mất sự kiện trace | lost=0 ở mọi lượt | lost=0 ở mọi lượt |
| Shell sau suite | Hoạt động | Hoạt động |
| Trace và thứ tự thuật toán | Thành đánh giá | Thành đánh giá |

Số sự kiện mỗi lượt workload nằm trong khoảng 251 đến 268.

Ghi chú về log CPUS=4: có một dòng "exec ... failed". Dòng này
chứa các ký tự điều khiển Ctrl+C và Ctrl+X dính vào tên lệnh do
thao tác bàn phím khi nhập, nên shell không tìm được chương trình.
Dòng echo cuối log hiển thị "cpus1" vì sửa dòng lệnh bằng phím
mũi tên, mà console xv6 không hỗ trợ. Cả hai không phải lỗi kernel.
Các lượt rt_compare quanh đó đều PASS.

## Minh chứng

- ../logs/dung-build.log
- ../logs/dung-version.txt
- ../logs/dung-qemu-cpus1.log
- ../logs/dung-qemu-cpus4.log

Bản chạy gồm thay đổi chưa commit tại thời điểm test; trạng thái
đầy đủ nằm trong dung-version.txt.

## Giới hạn

PAIRED WORKLOAD RESULT: PASS chỉ cho biết hiệu chuẩn hợp lệ,
worker của cả hai policy hoàn tất, xuất CSV đủ và policy được khôi
phục. Chưa kết luận không miss deadline, vòng đời job đúng hoặc
thuật toán chọn đúng; các kết luận đó do Thành tính từ CSV.

Số block là ước lượng từ thời gian trôi qua, không phải bảo đảm
WCET. Ba mẫu hiệu chuẩn không thay benchmark hiệu năng 5 mẫu.
RT vẫn ghim CPU 0, CPUS=4 không biến thành scheduler RT đa lõi.
Chưa có đo trang/inode, usertests, grind và stress lặp ít nhất
10 lượt cho workload; các mục này thuộc phiên nghiệm thu tích hợp.
Chưa triển khai stretch 1 ms tick và CBS.
