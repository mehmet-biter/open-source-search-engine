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
        api = gigablast.GigablastAPI(gb_config)
        api.status()
    except requests.exceptions.ConnectionError:
        pytest.skip('Gigablast instance down')

    def finalize():
        pass

    request.addfinalizer(finalize)
    return gb_config


@pytest.fixture(scope='function')
def gb_api(request, gb):
    api = gigablast.GigablastAPI(gb)

    def finalize():
        api.finalize()

    request.addfinalizer(finalize)
    return api
