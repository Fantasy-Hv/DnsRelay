# DNS报文转换逻辑
## 报文结构
dns报文分为控制和数据两部分
这里主要考虑控制部分，即协议中的header节
## header.QR
区分请求报文和响应报文
## 请求报文解析
### header.Opcode
1.query --> 服务器要根据问题中的域名查询ip
2.status --> 返回服务器状态
3.iquery --> 返回NOTIMP包
### header.RD
当Opcode为query,且本机无法回答时：
如果RD为1,需要向上游发送请求
如果RD为0：

## 响应报文解析
### header.RA
### header.Rcode



