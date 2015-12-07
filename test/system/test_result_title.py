import pytest
import mimetypes


def verify_title(gb_api, httpserver, filename, expected_title, custom_filename=''):
    # guess content type
    content_type = mimetypes.guess_type(filename)[0]

    httpserver.serve_content(content=open(filename, 'rb').read(),
                             headers={'content-type': content_type})

    # format url
    file_url = httpserver.url + '/' + custom_filename

    # add url
    assert gb_api.add_url(file_url) == True

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    assert result['results'][0]['title'] == expected_title


@pytest.mark.parametrize('filename, expected_title', [
    # filename                         expected_title
    ('title_exist.html',               'Title for html (in title)'),
])
def test_title_html(gb_api, httpserver, filename, expected_title):
    verify_title(gb_api, httpserver, 'data/html/' + filename, expected_title)


@pytest.mark.parametrize('filename, expected_title', [
    # filename                         expected_title
    ('title_emoticon_start.html',      'The quick brown fox jumps over the lazy dog'),
    ('title_emoticon_middle.html',     'The quick brown fox jumps over the lazy dog'),
    ('title_emoticon_end.html',        'The quick brown fox jumps over the lazy dog'),
])
def test_title_emoticon(gb_api, httpserver, filename, expected_title):
    verify_title(gb_api, httpserver, 'data/html/' + filename, expected_title)


@pytest.mark.parametrize('filename, expected_title, custom_filename', [
    # filename                          expected_title                              custom_filename
    ('title_word_no_prop.pdf',          'title.pdf',                                'title.pdf'),
    ('title_word_no_prop.pdf',          '',                                         ''),
    ('title_word_no_prop.pdf',          't.pdf',                                    't.pdf?v=1&b=user'),
    ('title_word_with_prop.pdf',        'Title for Microsoft Word (in title)',      'title.pdf'),
    ('title_word_with_prop.pdf',        'Title for Microsoft Word (in title)',      ''),
])
def test_title_office_pdf(gb_api, httpserver, filename, expected_title, custom_filename):
    verify_title(gb_api, httpserver, 'data/office/' + filename, expected_title, custom_filename=custom_filename)
