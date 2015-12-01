import requests


class GigablastConfig:
    def __init__(self, config):
        self.host = config['host']
        self.port = config['port']


class GigablastAPI:
    class _HTTPStatus:
        @staticmethod
        def compare(status, expected_status):
            return status[status.find('(')+1:status.find(')')] == expected_status

        @staticmethod
        def doc_force_delete():
            return 'Doc force deleted'

    def __init__(self, gb_config):
        self._config = gb_config
        self._add_urls = set()

    def finalize(self):
        # cleanup urls
        for url in self._add_urls:
            self.delete_url(url, True)

    def _get_url(self, path):
        return 'http://' + self._config.host + ':' + self._config.port + '/' + path

    @staticmethod
    def _apply_default_payload(payload):
        payload.setdefault('c', 'main')
        payload.setdefault('format', 'json')
        payload.setdefault('showinput', '0')

    def _check_http_status(self, e, expected_status):
        # hacks to cater for inject returning invalid status line
        if (len(e.args) == 1 and
                type(e.args[0]) == requests.packages.urllib3.exceptions.ProtocolError and
                len(e.args[0].args) == 2):
            import http.client
            if type(e.args[0].args[1]) == http.client.BadStatusLine:
                if self._HTTPStatus.compare(str(e.args[0].args[1]), expected_status):
                    return True
        return False

    def _add_url(self, url, payload=None):
        if not payload:
            payload = {}

        self._apply_default_payload(payload)

        payload.update({'urls': url})

        response = requests.get(self._get_url('admin/addurl'), params=payload)

        return response.json()

    def _inject(self, url, payload=None):
        if not payload:
            payload = {}

        self._apply_default_payload(payload)

        payload.update({'url': url})

        response = requests.get(self._get_url('admin/inject'), params=payload)

        # inject doesn't seem to wait until document is completely indexed
        from time import sleep
        sleep(0.1)

        return response.json()

    def add_url(self, url, real_time=True):
        self._add_urls.add(url)

        if real_time:
            return self._inject(url)['response']['statusCode'] == 0
        else:
            return self._add_url(url)['response']['statusCode'] == 0

    def delete_url(self, url, finalizer=False):
        if not finalizer:
            self._add_urls.discard(url)

        payload = {'deleteurl': '1'}

        try:
            self._inject(url, payload)
        except requests.exceptions.ConnectionError as e:
            # delete url returns invalid HTTP status line
            return self._check_http_status(e, self._HTTPStatus.doc_force_delete())

        return False

    def search(self, query, payload=None):
        if not payload:
            payload = {}

        self._apply_default_payload(payload)

        payload.update({'q': query})

        response = requests.get(self._get_url('search'), params=payload)

        return response.json()

    def status(self, payload=None):
        if not payload:
            payload = {}

        self._apply_default_payload(payload)

        response = requests.get(self._get_url('admin/status'), params=payload)

        return response.json()
