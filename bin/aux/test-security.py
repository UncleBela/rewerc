#!/usr/bin/env python3
"""Native account/file boundary integration checks; fixtures never use real notes."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import sqlite3
import subprocess
import tempfile
import time
import unittest
from urllib.parse import urlencode, quote

ROOT = Path(__file__).resolve().parents[2]
PASSWORD = 'fixture password for native tests'

class Security(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='security-test-', dir=ROOT/'var')
        cls.base = Path(cls.temp.name)
        cls.app = cls.base/'install'
        for folder in ('bin', 'lib', 'tpl', 'apps', 'pub'):
            shutil.copytree(ROOT/folder, cls.app/folder)
        (cls.app/'etc').mkdir()
        shutil.copyfile(ROOT/'etc/initrc', cls.app/'etc/initrc')
        (cls.app/'etc/initrc.local').write_text('PLAN9=/opt/plan9\nplan9port=$PLAN9\npath=($PLAN9/bin . ./bin ./bin/contrib /usr/bin /bin)\nTMPDIR=$werc_root/var/tmp\nformatter=(md2html)\nrcargs=-I\ndebug=()\nenabled_apps=()\n')
        cls.site = cls.app/'sites/fixture'
        (cls.site/'_werc').mkdir(parents=True)
        (cls.site/'_werc/config').write_text("siteTitle='Protected fixture'\nconf_enable_mathjax\n")
        (cls.site/'index.md').write_text('# PROTECTED-PAGE-MARKER\n\n<div>Authored HTML remains allowed.</div>\n')
        (cls.site/'attachment.pdf').write_bytes(b'%PDF-1.4 PROTECTED_ATTACHMENT_MARKER')
        (cls.site/'robots.txt').write_text('PROTECTED_ROBOTS_MARKER')
        cls.authdir = cls.base/'accounts'; cls.authdir.mkdir(mode=0o700)
        cls.env = dict(os.environ, WERC_AUTH_DIR=str(cls.authdir), WERC_DEVELOPMENT='yes')
        cls.env.pop('REQUEST_METHOD', None)
        cls.provision()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def provision(cls, disable=False):
        args = [str(cls.app/'bin/contrib/werc-guard'), '--disable-user' if disable else '--add-user', 'fixture']
        if not disable: args.append('--password-stdin')
        result = subprocess.run(args, cwd=cls.app, env=cls.env, input=PASSWORD+'\n', text=True, capture_output=True, timeout=10)
        if result.returncode: raise RuntimeError(result.stderr)

    def setUp(self):
        with sqlite3.connect(self.authdir/'auth.sqlite') as db:
            db.execute('DELETE FROM sessions'); db.execute('DELETE FROM limits')
        self.jar = ''

    def cgi(self, path='/', method='GET', data='', cookie=None, extra=None):
        env = dict(self.env, REQUEST_METHOD=method, REQUEST_URI=path, SERVER_NAME='localhost', SERVER_PORT='8080', WERC_SITE='fixture', PLAN9='/opt/plan9', REMOTE_ADDR='127.0.0.1', QUERY_STRING='', HTTP_COOKIE=self.jar if cookie is None else cookie)
        if method == 'POST':
            env.update(CONTENT_TYPE='application/x-www-form-urlencoded', CONTENT_LENGTH=str(len(data.encode())), HTTP_ORIGIN='http://localhost:8080')
        env.update(extra or {})
        result = subprocess.run([str(self.app/'bin/werc.cgi')], cwd=self.app, env=env, input=data, text=True, capture_output=True, timeout=30)
        output = result.stdout.replace('\r\n', '\n')
        head, _, body = output.partition('\n\n')
        match = re.search(r'^Status: (\d+)', head, re.M)
        return int(match[1]) if match else 200, head, body, result.stderr

    def challenge(self, extra=None):
        status, headers, body, err = self.cgi('/_users/login', extra=extra)
        self.assertEqual(status, 200, (headers, body, err))
        match = re.search(r'Set-Cookie: (werc_session=[0-9a-f]{64})', headers)
        self.assertIsNotNone(match)
        self.jar = match[1]
        return re.search(r'name="csrf" value="([0-9a-f]{64})"', body)[1]

    def login(self, extra=None):
        csrf = self.challenge(extra)
        old = self.jar
        data = urlencode(dict(username='fixture', password=PASSWORD, csrf=csrf))
        status, headers, body, err = self.cgi('/_users/login', 'POST', data, extra=extra)
        self.assertEqual(status, 303, (headers, body, err))
        self.jar = re.search(r'Set-Cookie: (werc_session=[0-9a-f]{64})', headers)[1]
        self.assertNotEqual(old, self.jar)
        self.assertIn('HttpOnly; SameSite=Strict', headers)
        self.assertNotIn(PASSWORD, headers+body+err)
        return old

    def test_no_content_or_attachment_without_session(self):
        for path in ('/', '/attachment.pdf', '/robots.txt', '/sitemap', '/pub/other.css'):
            status, headers, body, err = self.cgi(path)
            self.assertEqual(status, 303, path)
            self.assertIn('Location: /_users/login', headers)
            self.assertNotIn('PROTECTED_', body)
        status, headers, body, err = self.cgi('/', 'HEAD')
        self.assertEqual(body, '')
        self.assertEqual(self.cgi('/pub/style/style.css')[0], 200)
        self.assertEqual(self.cgi('/index.md')[0], 404)
        self.assertEqual(self.cgi('/_werc/config')[0], 404)

    def test_authenticated_page_and_attachment(self):
        old = self.login()
        status, headers, body, err = self.cgi('/')
        self.assertEqual(status, 200, err)
        self.assertIn('PROTECTED-PAGE-MARKER', body)
        self.assertIn('<div>Authored HTML remains allowed.</div>', body)
        status, headers, body, err = self.cgi('/attachment.pdf')
        self.assertEqual(status, 200)
        self.assertIn('application/pdf', headers)
        self.assertIn('Cache-Control: no-store', headers)
        self.assertIn('PROTECTED_ATTACHMENT_MARKER', body)
        self.assertEqual(self.cgi('/', cookie=old)[0], 303)
        self.assertEqual(self.cgi('/attachment.pdf', 'HEAD')[2], '')

    def test_logout_csrf_and_revocation(self):
        self.login()
        status, headers, body, err = self.cgi('/_users/logout')
        csrf = re.search(r'name="csrf" value="([0-9a-f]{64})"', body)[1]
        self.assertEqual(self.cgi('/')[0], 200)  # GET logout did not mutate.
        self.assertEqual(self.cgi('/_users/logout', 'POST', 'csrf=forged')[0], 403)
        status, headers, body, err = self.cgi('/_users/logout', 'POST', 'csrf='+csrf)
        self.assertEqual(status, 303)
        self.assertIn('Max-Age=0', headers)
        self.assertEqual(self.cgi('/')[0], 303)

    def test_csrf_origin_and_form_limits(self):
        csrf = self.challenge()
        data = urlencode(dict(username='fixture', password=PASSWORD, csrf=csrf))
        for extra in ({'HTTP_ORIGIN':'https://attacker.invalid'}, {'HTTP_ORIGIN':''}):
            self.assertEqual(self.cgi('/_users/login','POST',data,extra=extra)[0],403)
        self.assertEqual(self.cgi('/_users/login','POST',data+'&csrf='+csrf)[0],400)
        self.assertEqual(self.cgi('/_users/login','POST',data,extra={'CONTENT_LENGTH':'65537'})[0],413)
        self.assertEqual(self.cgi('/_users/login','POST',data,extra={'CONTENT_TYPE':'text/plain'})[0],415)
        self.assertEqual(self.cgi('/_users/login','POST',data+'&x=%C0%AF')[0],400)
        self.assertEqual(self.cgi('/_users/login','POST',data+''.join('&p%d=x'%i for i in range(33)))[0],400)

    def test_wrong_password_and_throttling(self):
        csrf = self.challenge()
        data = urlencode(dict(username='fixture', password='wrong password', csrf=csrf))
        for _ in range(10):
            status, headers, body, err = self.cgi('/_users/login','POST',data)
            self.assertEqual(status,403)
            self.assertNotIn('Set-Cookie:',headers)
            self.assertNotIn('wrong password',body+err)
        self.assertEqual(self.cgi('/_users/login','POST',data)[0],429)

    def test_expiry_idle_and_scope(self):
        self.login()
        self.assertEqual(self.cgi('/',extra={'SERVER_PORT':'8081'})[0],303)
        with sqlite3.connect(self.authdir/'auth.sqlite') as db:
            db.execute('UPDATE sessions SET touched=?',(int(time.time())-1801,))
        self.assertEqual(self.cgi('/')[0],303)
        self.jar='';self.login()
        with sqlite3.connect(self.authdir/'auth.sqlite') as db:
            db.execute('UPDATE sessions SET expires=?',(int(time.time())-1,))
        self.assertEqual(self.cgi('/')[0],303)

    def test_secure_cookie_on_https(self):
        extra={'HTTPS':'on','HTTP_ORIGIN':'https://localhost:8080'}
        csrf=self.challenge({'HTTPS':'on'})
        data=urlencode(dict(username='fixture',password=PASSWORD,csrf=csrf))
        status,headers,body,err=self.cgi('/_users/login','POST',data,extra=extra)
        self.assertEqual(status,303)
        self.assertIn('; Secure',headers)

    def test_password_reset_and_disable_revoke(self):
        self.login()
        self.provision()
        self.assertEqual(self.cgi('/')[0],303)
        self.jar='';self.login()
        try:
            self.provision(disable=True)
            self.assertEqual(self.cgi('/')[0],303)
        finally:
            self.provision()

    def test_private_state_permissions(self):
        try:
            os.chmod(self.authdir/'auth.sqlite',0o644)
            self.assertEqual(self.cgi('/_users/login')[0],503)
        finally:
            os.chmod(self.authdir/'auth.sqlite',0o600)

    def test_all_content_writes_stay_disabled(self):
        self.login()
        before=(self.site/'index.md').read_bytes()
        for path in ('/','/attachment.pdf','/new_post','/_users/register'):
            self.assertIn(self.cgi(path,'POST','body=changed')[0],(404,405))
        self.assertEqual((self.site/'index.md').read_bytes(),before)

    def test_symlinks_rejected_without_policy(self):
        self.login()
        target=self.base/'outside';target.mkdir(exist_ok=True)
        (target/'SECRET_FILENAME.md').write_text('SECRET_BODY_MARKER')
        link=self.site/'linked';link.symlink_to(target,target_is_directory=True)
        try:
            for path in ('/', '/linked/', '/linked/SECRET_FILENAME'):
                status,headers,body,err=self.cgi(path)
                self.assertEqual(status,403)
                self.assertNotIn('SECRET_',body)
        finally:
            link.unlink()

    def test_no_unrelated_scan_and_directory_limits(self):
        self.login()
        large=self.site/'unvisited';large.mkdir()
        try:
            for i in range(2050):(large/('note%d.md'%i)).touch()
            self.assertEqual(self.cgi('/')[0],200)
            self.assertEqual(self.cgi('/unvisited/')[0],413)
            self.assertEqual(self.cgi('/'+'/'.join(['level']*34)+'/')[0],413)
        finally:
            shutil.rmtree(large)  # This is our real disposable fixture directory.

    def test_swap_after_validation_cannot_list_outside(self):
        self.login()
        folder=self.site/'swap';folder.mkdir()
        (folder/'safe.md').write_text('safe')
        outside=self.base/'outside-race';outside.mkdir()
        (outside/'SECRET_FILENAME.md').write_text('SECRET_BODY_MARKER')
        fake=self.base/'plan9/bin';fake.mkdir(parents=True)
        wrapper=fake/'rc'
        wrapper.write_text('#!/bin/sh\nset -eu\nmv "$RACE_DIRECTORY" "$RACE_DIRECTORY.saved"\nln -s "$RACE_OUTSIDE" "$RACE_DIRECTORY"\nexport PLAN9=/opt/plan9\nexec /opt/plan9/bin/rc "$@"\n')
        wrapper.chmod(0o755)
        try:
            status,headers,body,err=self.cgi('/swap/',extra={'PLAN9':str(fake.parent),'RACE_DIRECTORY':str(folder),'RACE_OUTSIDE':str(outside)})
            self.assertNotIn('SECRET_',body)
            self.assertIn('pinned file access failed',err)
        finally:
            if folder.is_symlink():folder.unlink()
            if Path(str(folder)+'.saved').exists():Path(str(folder)+'.saved').rename(folder)
            shutil.rmtree(folder)
            shutil.rmtree(fake.parent)

    def test_dedicated_account_required_by_default(self):
        self.login()
        status,headers,body,err=self.cgi('/',extra={'WERC_DEVELOPMENT':'','HTTP_WERC_DEVELOPMENT':'yes'})
        self.assertEqual(status,503)
        self.assertNotIn('PROTECTED-PAGE-MARKER',body)

    def test_shared_work_budget_across_helpers(self):
        self.login()
        folder=self.site/'budget';folder.mkdir()
        for i in range(200):(folder/('note%d.md'%i)).touch()
        fake=self.base/'budget-plan9/bin';fake.mkdir(parents=True)
        wrapper=fake/'rc'
        wrapper.write_text('#!/bin/sh\nset -eu\ni=0\nwhile [ "$i" -lt 100 ]; do\n if ! "$GUARD" --list sites/fixture/budget/ >/dev/null; then\n  printf "Content-Type: text/plain\\n\\nBUDGET-REJECTED\\n"\n  exit 0\n fi\n i=$((i+1))\ndone\nprintf "Content-Type: text/plain\\n\\nBUDGET-NOT-ENFORCED\\n"\n')
        wrapper.chmod(0o755)
        try:
            status,headers,body,err=self.cgi('/',extra={'PLAN9':str(fake.parent),'GUARD':str(self.app/'bin/contrib/werc-guard')})
            self.assertIn('BUDGET-REJECTED',body)
            self.assertNotIn('BUDGET-NOT-ENFORCED',body)
        finally:
            shutil.rmtree(folder)
            shutil.rmtree(fake.parent)

    def test_nested_utf8_browsing(self):
        self.login()
        nested=self.site/'Árvíztűrő (I. ea.)'/'Mélyebb mappa'
        nested.mkdir(parents=True)
        (nested/'Első jegyzet.md').write_text('---\ntitle: Nested title\n---\n# NESTED-BODY-MARKER\n')
        try:
            path='/'+quote(str(nested.relative_to(self.site)))+'/'
            self.assertIn(quote('Árvíztűrő (I. ea.)'),self.cgi('/')[2])
            status,headers,body,err=self.cgi(path+quote('Első jegyzet'))
            self.assertEqual(status,200,err)
            self.assertIn('NESTED-BODY-MARKER',body)
            self.assertIn('<title>Nested title</title>',body)
            self.assertIn(quote('Első jegyzet'),self.cgi(path)[2])
        finally:
            shutil.rmtree(nested.parent)

if __name__=='__main__':unittest.main(verbosity=2)
