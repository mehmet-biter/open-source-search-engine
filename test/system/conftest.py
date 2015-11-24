import pytest
import configparser
import requests
import os
import gigablast


@pytest.fixture(scope='session', autouse=True)
def gb(request):
    # change working dir
    os.chdir(os.path.dirname(__file__))

    config = configparser.ConfigParser()
    config.read('config.ini')

    gb_config = gigablast.GigablastConfig(config['gigablast'])

    # verify gb is running
    try:
        requests.get('http://' + gb_config.host + ':' + gb_config.port)
    except requests.exceptions.ConnectionError as e:
        pytest.fail(msg=e)

    def finalize():
        pass

    request.addfinalizer(finalize)
    return gb_config
