#!/usr/bin/env python3
import sys
import smtplib
from email.mime.text import MIMEText

def send_email(to, subject, body):
    try:
        msg = MIMEText(body)
        msg['Subject'] = subject
        msg['From'] = '2656086785@qq.com'
        msg['To'] = to

        s = smtplib.SMTP_SSL('smtp.qq.com', 465)
        s.login('2656086785@qq.com', 'abuumugfvxcsecgd')
        s.send_message(msg)
        s.quit()
        return True
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return False

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: send_email.py <to> <subject>", file=sys.stderr)
        sys.exit(1)
    
    to = sys.argv[1]
    subject = sys.argv[2]
    body = sys.stdin.read()
    
    if send_email(to, subject, body):
        sys.exit(0)
    else:
        sys.exit(1)