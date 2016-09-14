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

import os
import sys
import stat
import config
import signal
import subprocess
import time
import pytest
from util import unindent
from util import run_shell
from requests import HTTPError

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

    secpid = os.fork()
    if secpid == 0:
        if subprocess.call([resp_path, "--uid=0", "--gid=0"]) != 0:
            raise Exception("sssd_secrets failed to start")

    time.sleep(1)

    def sec_teardown():
        if secpid == 0:
            return

        os.kill(secpid, signal.SIGTERM)
        for file in os.listdir(config.SECDB_PATH):
            os.unlink(config.SECDB_PATH + "/" + file)
    request.addfinalizer(sec_teardown)


@pytest.fixture
def setup_for_secrets(request):
    """
    Just set up the local provider for tests and enable the secrets
    responder
    """
    conf = unindent("""\
        [sssd]
        domains             = local
        services            = nss, pam

        [domain/local]
        id_provider=local
    """).format(**locals())

    create_conf_fixture(request, conf)
    create_sssd_secrets_fixture(request)
    return None


@pytest.fixture
def secrets_cli(request):
    sock_path = os.path.join(config.RUNSTATEDIR, "secrets.socket")
    cli = SecretsLocalClient(sock_path=sock_path)
    return cli


def test_crd_ops(setup_for_secrets, secrets_cli):
    """
    Test that the basic Create, Retrieve, Delete operations work
    """
    cli = secrets_cli

    # Listing a totally empty database yields a 404 error, no secrets are there
    with pytest.raises(HTTPError) as err404:
        secrets = cli.list_secrets()
    assert err404.value.message.startswith("404")

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
    assert err409.value.message.startswith("409")

    # Delete a secret
    cli.del_secret("foo")
    with pytest.raises(HTTPError) as err404:
        fooval = cli.get_secret("foo")
    assert err404.value.message.startswith("404")

    # Delete a non-existent secret must yield a 404
    with pytest.raises(HTTPError) as err404:
        cli.del_secret("foo")
    assert err404.value.message.startswith("404")


def test_containers(setup_for_secrets, secrets_cli):
    cli = secrets_cli

    # No trailing slash, no game..
    with pytest.raises(HTTPError) as err400:
        cli.create_container("mycontainer")
    assert err400.value.message.startswith("400")

    cli.create_container("mycontainer/")
    cli.set_secret("mycontainer/foo", "containedfooval")
    assert cli.get_secret("mycontainer/foo") == "containedfooval"

    # Removing a non-empty container should not succeed
    with pytest.raises(HTTPError) as err409:
        cli.del_secret("mycontainer/")
    assert err409.value.message.startswith("409")

    # Try removing the secret first, then the container
    cli.del_secret("mycontainer/foo")
    cli.del_secret("mycontainer/")
