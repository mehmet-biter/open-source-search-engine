import pytest
import mimetypes


@pytest.mark.parametrize('file_location, url_location, expected_title', [
    # file_location                     url_location                        expected_title
    ('test_word_no_properties.pdf',     'test_word_no_properties.pdf',      'test_word_no_properties.pdf'),
    ('test_word_no_properties.pdf',     '',                                 ''),
    ('test_word_with_properties.pdf',   'test_word_with_properties.pdf',    'Title for Microsoft Word (in title)'),
])
def test_index_documents_office(gb_api, httpserver, file_location, url_location, expected_title):
    httpserver.serve_content(content=open('data/office/' + file_location, 'rb').read(),
                             headers={'content-type': mimetypes.guess_type(file_location)[0]})

    # format url
    file_url = httpserver.url + '/' + url_location

    # add url
    assert gb_api.add_url(file_url) == True

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    assert result['results'][0]['title'] == expected_title
