#
# Secrets responder integration tests
#
# Copyright (c) 2016 Red Hat, Inc.
#
# This is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 only
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

from __future__ import print_function
import os
import stat
import sys
import config
import signal
import subprocess
import time
import socket
import pytest
from requests import HTTPError

from util import unindent
from secrets import SecretsLocalClient


def create_conf_fixture(request, contents):
    """Generate sssd.conf and add teardown for removing it"""
    conf = open(config.CONF_PATH, "w")
    conf.write(contents)
    conf.close()
    os.chmod(config.CONF_PATH, stat.S_IRUSR | stat.S_IWUSR)
    request.addfinalizer(lambda: os.unlink(config.CONF_PATH))


def create_sssd_secrets_fixture(request):
    if subprocess.call(['sssd', "--genconf"]) != 0:
        raise Exception("failed to regenerate confdb")

    resp_path = os.path.join(config.LIBEXEC_PATH, "sssd", "sssd_secrets")
    if not os.access(resp_path, os.X_OK):
        # It would be cleaner to use pytest.mark.skipif on the package level
        # but upstream insists on supporting RHEL-6.
        pytest.skip("No Secrets responder, skipping")

    secpid = os.fork()
    assert secpid >= 0

    if secpid == 0:
        if subprocess.call([resp_path, "--uid=0", "--gid=0"]) != 0:
            print("sssd_secrets failed to start")
            sys.exit(99)
    else:
        sock_path = os.path.join(config.RUNSTATEDIR, "secrets.socket")
        sck = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(1, 10):
            try:
                sck.connect(sock_path)
            except:
                time.sleep(0.1)
            else:
                break
        sck.close()

        assert os.path.exists(sock_path)

    def sec_teardown():
        if secpid == 0:
            return

        os.kill(secpid, signal.SIGTERM)
        for secdb_file in os.listdir(config.SECDB_PATH):
            os.unlink(config.SECDB_PATH + "/" + secdb_file)
    request.addfinalizer(sec_teardown)
    return secpid


@pytest.fixture
def setup_for_secrets(request):
    """
    Just set up the local provider for tests and enable the secrets
    responder
    """
    conf = unindent("""\
        [sssd]
        domains = local
        services = nss

        [domain/local]
        id_provider = local

        [secrets]
        max_secrets = 10
        max_payload_size = 2
    """).format(**locals())

    create_conf_fixture(request, conf)
    create_sssd_secrets_fixture(request)
    return None


def get_secrets_socket():
    return os.path.join(config.RUNSTATEDIR, "secrets.socket")


@pytest.fixture
def secrets_cli(request):
    sock_path = get_secrets_socket()
    cli = SecretsLocalClient(sock_path=sock_path)
    return cli


@pytest.fixture
def curlwrap_tool(request):
    curlwrap_path = os.path.join(config.ABS_BUILDDIR,
                                 "..", "..", "..", "tcurl-test-tool")
    if os.access(curlwrap_path, os.X_OK):
        return curlwrap_path

    return None


def test_crd_ops(setup_for_secrets, secrets_cli):
    """
    Test that the basic Create, Retrieve, Delete operations work
    """
    cli = secrets_cli

    # Listing a totally empty database yields a 404 error, no secrets are there
    with pytest.raises(HTTPError) as err404:
        secrets = cli.list_secrets()
    assert str(err404.value).startswith("404")

    # Set some value, should succeed
    cli.set_secret("foo", "bar")

    fooval = cli.get_secret("foo")
    assert fooval == "bar"

    # Listing secrets should work now as well
    secrets = cli.list_secrets()
    assert len(secrets) == 1
    assert "foo" in secrets

    # Overwriting a secret is an error
    with pytest.raises(HTTPError) as err409:
        cli.set_secret("foo", "baz")
    assert str(err409.value).startswith("409")

    # Delete a secret
    cli.del_secret("foo")
    with pytest.raises(HTTPError) as err404:
        fooval = cli.get_secret("foo")
    assert str(err404.value).startswith("404")

    # Delete a non-existent secret must yield a 404
    with pytest.raises(HTTPError) as err404:
        cli.del_secret("foo")
    assert str(err404.value).startswith("404")

    # Don't allow storing more secrets after reaching the max
    # number of entries.
    MAX_SECRETS = 10

    sec_value = "value"
    for x in range(MAX_SECRETS):
        cli.set_secret(str(x), sec_value)

    with pytest.raises(HTTPError) as err507:
        cli.set_secret(str(MAX_SECRETS), sec_value)
    assert str(err507.value).startswith("507")

    # Delete all stored secrets used for max secrets tests
    for x in range(MAX_SECRETS):
        cli.del_secret(str(x))

    # Don't allow storing a secrets which has a payload larger
    # than max_payload_size
    KILOBYTE = 1024
    MAX_PAYLOAD_SIZE = 2 * KILOBYTE

    sec_value = "x" * MAX_PAYLOAD_SIZE

    cli.set_secret("foo", sec_value)

    sec_value += "x"
    with pytest.raises(HTTPError) as err413:
        cli.set_secret("bar", sec_value)
    assert str(err413.value).startswith("413")


def run_curlwrap_tool(args, exp_http_code):
    cmd = subprocess.Popen(args,
                           stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    out, _ = cmd.communicate()

    assert cmd.returncode == 0

    out = out.decode('utf-8')
    exp_http_code_str = "Request HTTP code: %d" % exp_http_code
    assert exp_http_code_str in out

    return out


def test_curlwrap_crd_ops(setup_for_secrets,
                          curlwrap_tool):
    """
    Test that the basic Create, Retrieve, Delete operations work using our
    tevent libcurl code
    """
    if not curlwrap_tool:
        pytest.skip("The tcurl tool is not available, skipping test")
    sock_path = get_secrets_socket()

    # listing an empty DB yields a 404
    run_curlwrap_tool([curlwrap_tool,
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/'],
                      404)

    # listing a non-existent secret yields a 404
    run_curlwrap_tool([curlwrap_tool,
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/foo'],
                      404)

    # set a secret foo:bar
    run_curlwrap_tool([curlwrap_tool, '-p',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/foo',
                       'bar'],
                      200)

    # list secrets
    output = run_curlwrap_tool([curlwrap_tool,
                                '-v', '-s', sock_path,
                                'http://localhost/secrets/'],
                               200)
    assert "foo" in output

    # get the foo secret
    output = run_curlwrap_tool([curlwrap_tool,
                                '-v', '-s', sock_path,
                                'http://localhost/secrets/foo'],
                               200)
    assert "bar" in output

    # Overwriting a secret is an error
    run_curlwrap_tool([curlwrap_tool, '-p',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/foo',
                       'baz'],
                      409)

    # Delete a secret
    run_curlwrap_tool([curlwrap_tool, '-d',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/foo'],
                      200)

    # Delete a non-existent secret must yield a 404
    run_curlwrap_tool([curlwrap_tool, '-d',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/foo'],
                      404)

    # Create a container
    run_curlwrap_tool([curlwrap_tool, '-o',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/cont/'],
                      200)

    # set a secret foo:bar
    run_curlwrap_tool([curlwrap_tool, '-p',
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/cont/cfoo',
                       'foo_under_cont'],
                      200)

    # list secrets
    output = run_curlwrap_tool([curlwrap_tool,
                                '-v', '-s', sock_path,
                                'http://localhost/secrets/cont/'],
                               200)
    assert "cfoo" in output

    # get the foo secret
    output = run_curlwrap_tool([curlwrap_tool,
                                '-v', '-s', sock_path,
                                'http://localhost/secrets/cont/cfoo'],
                               200)
    assert "foo_under_cont" in output


def test_curlwrap_parallel(setup_for_secrets,
                           curlwrap_tool):
    """
    The tevent libcurl wrapper is meant to be non-blocking. Test
    its operation in parallel.
    """
    if not curlwrap_tool:
        pytest.skip("The tcurl tool is not available, skipping test")
    sock_path = get_secrets_socket()

    secrets = dict()
    nsecrets = 10

    for i in range(0, nsecrets):
        secrets["key" + str(i)] = "value" + str(i)

    args = [curlwrap_tool, '-p', '-v', '-s', sock_path]
    for skey, svalue in secrets.items():
        args.extend(['http://localhost/secrets/%s' % skey, svalue])
    run_curlwrap_tool(args, 200)

    output = run_curlwrap_tool([curlwrap_tool,
                                '-v', '-s', sock_path,
                                'http://localhost/secrets/'],
                               200)
    for skey in secrets:
        assert skey in output

    args = [curlwrap_tool, '-g', '-v', '-s', sock_path]
    for skey in secrets:
        args.extend(['http://localhost/secrets/%s' % skey])
    output = run_curlwrap_tool(args, 200)

    for svalue in secrets.values():
        assert svalue in output

    args = [curlwrap_tool, '-d', '-v', '-s', sock_path]
    for skey in secrets:
        args.extend(['http://localhost/secrets/%s' % skey])
    output = run_curlwrap_tool(args, 200)

    run_curlwrap_tool([curlwrap_tool,
                       '-v', '-s', sock_path,
                       'http://localhost/secrets/'],
                      404)


def test_containers(setup_for_secrets, secrets_cli):
    """
    Test that storing secrets inside containers works
    """
    cli = secrets_cli

    # No trailing slash, no game..
    with pytest.raises(HTTPError) as err400:
        cli.create_container("mycontainer")
    assert str(err400.value).startswith("400")

    cli.create_container("mycontainer/")
    cli.set_secret("mycontainer/foo", "containedfooval")
    assert cli.get_secret("mycontainer/foo") == "containedfooval"

    # Removing a non-empty container should not succeed
    with pytest.raises(HTTPError) as err409:
        cli.del_secret("mycontainer/")
    assert str(err409.value).startswith("409")

    # Try removing the secret first, then the container
    cli.del_secret("mycontainer/foo")
    cli.del_secret("mycontainer/")

    # Don't allow creating a container after reaching the max nested level
    DEFAULT_CONTAINERS_NEST_LEVEL = 4
    container = "mycontainer"
    for x in range(DEFAULT_CONTAINERS_NEST_LEVEL):
        container += "%s/" % str(x)
        cli.create_container(container)

    container += "%s/" % str(DEFAULT_CONTAINERS_NEST_LEVEL)
    with pytest.raises(HTTPError) as err406:
        cli.create_container(container)
    assert str(err406.value).startswith("406")
