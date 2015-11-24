import requests


class GigablastConfig:
    def __init__(self, config):
        self.host = config['host']
        self.port = config['port']


class GigablastSearch:
    def __init__(self, gb_config):
        self.config = gb_config

    def search(self, query, payload=None):
        if not payload:
            payload = {}

        payload.update({'format': 'json'})
        payload.update({'q': query})

        response = requests.get('http://' + self.config.host + ':' + self.config.port + '/search', params=payload)

        return response.json()
