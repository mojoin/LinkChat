import socket
import time

HOST = '10.82.112.118'
PORT = 9527

def recv_one_line(sock, timeout=2.0):
    """收一行(以 \n 结尾),返回去掉 \n 的字符串"""
    sock.settimeout(timeout)
    buf = b''
    while True:
        c = sock.recv(1)
        if not c:
            return buf.decode(errors='replace')
        buf += c
        if c == b'\n':
            return buf.decode(errors='replace').rstrip('\n')

def send_line(sock, line):
    sock.sendall((line + '\n').encode())

def expect_json(sock, timeout=2.0):
    return recv_one_line(sock, timeout)

def main():
    s = socket.socket()
    s.connect((HOST, PORT))

    # 1) 握手:服务器先发 SERVER_HELLO 1
    hello = recv_one_line(s)
    print('[1]', repr(hello))
    assert hello == 'SERVER_HELLO 1', f'unexpected: {hello!r}'

    # 2) 客户端回 CLIENT_HELLO 1
    send_line(s, 'CLIENT_HELLO 1')
    welcome = recv_one_line(s)
    print('[2]', repr(welcome))
    assert welcome == 'SERVER_WELCOME 1', f'unexpected: {welcome!r}'

    # 3) 登录 10001/123456
    send_line(s, '{"type":"login","uid":10001,"password":"123456"}')
    r = expect_json(s)
    print('[3 login]', r)
    assert '"ok":true' in r, 'login failed'

    # 4) 搜自己 10001 → state应该是 "already_friend"(虽然搜自己不报错)
    send_line(s, '{"type":"search_user","uid":10001}')
    r = expect_json(s)
    print('[4 search self]', r)
    assert '"ok":true' in r
    assert '"state":"already_friend"' in r or '"state":"none"' in r  # 10001 的 friends里有自己?这里没有

    # 5) 搜 10002 → 应该是 already_friend (因为 10001 的 friends 里有 10002)
    send_line(s, '{"type":"search_user","uid":10002}')
    r = expect_json(s)
    print('[5 search friend]', r)
    assert '"ok":true' in r
    assert '"state":"already_friend"' in r, f'expected already_friend, got: {r}'

    # 6) 搜不存在的 99999
    send_line(s, '{"type":"search_user","uid":99999}')
    r = expect_json(s)
    print('[6 search not exist]', r)
    assert '"ok":false' in r
    assert 'user not found' in r

    # 7) 添加好友10002 (已经是好友 →失败)
    send_line(s, '{"type":"add_friend_request","to_uid":10002}')
    r = expect_json(s)
    print('[7 add already friend]', r)
    assert '"ok":false' in r
    assert 'already friends' in r

    # 8) 添加自己 → 失败
    send_line(s, '{"type":"add_friend_request","to_uid":10001}')
    r = expect_json(s)
    print('[8 add self]', r)
    assert '"ok":false' in r
    assert 'cannot add self' in r

    # 9) 添加 10004 (不存在)
    send_line(s, '{"type":"add_friend_request","to_uid":10004}')
    r = expect_json(s)
    print('[9 add not exist]', r)
    assert '"ok":false' in r
    assert 'user not found' in r

    # 10) 退出    s.close()
    print('\n[OK] 所有断言通过')

if __name__ == '__main__':
    main()
