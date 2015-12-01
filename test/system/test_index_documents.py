import pytest
import os


@pytest.mark.parametrize('file_location, url_with_file, content_type, expected_title', [
    # file_location                     url_with_file   content_type            expected_title
    ('test_word_no_properties.pdf',     True,           'application/pdf',      'test_word_no_properties.pdf'),
    ('test_word_no_properties.pdf',     False,          'application/pdf',      ''),
    ('test_word_with_properties.pdf',   True,           'application/pdf',      'Title for Microsoft Word (in title)'),
])
def test_index_documents_office(gb_api, httpserver, file_location, url_with_file, content_type, expected_title):
    httpserver.serve_content(content=open('data/office/' + file_location, 'rb').read(),
                             headers={'content-type': content_type})
    print(httpserver.url)

    # format url
    file_url = httpserver.url + '/'
    if url_with_file:
        file_url += os.path.basename(file_location)

    # add url
    assert gb_api.add_url(file_url) == True

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    assert result['results'][0]['title'] == expected_title
