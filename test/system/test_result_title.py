import pytest
import mimetypes


@pytest.mark.parametrize('filename, url, expected_title', [
    # filename                              url                 expected_title

    # test pdf
    ('office/title_word_no_prop.pdf',       'title.pdf',        'title.pdf'),
    ('office/title_word_no_prop.pdf',       '',                 ''),
    ('office/title_word_no_prop.pdf',       't.pdf?v=1&b=user', 't.pdf'),
    ('office/title_word_with_prop.pdf',     'title.pdf',        'Title for Microsoft Word (in title)'),
    ('office/title_word_with_prop.pdf',     '',                 'Title for Microsoft Word (in title)'),

    # test emoticon
    ('html/title_emoticon_start.html',      '',                 'The quick brown fox jumps over the lazy dog'),
    ('html/title_emoticon_middle.html',     '',                 'The quick brown fox jumps over the lazy dog'),
    ('html/title_emoticon_end.html',        '',                 'The quick brown fox jumps over the lazy dog'),

    # test title
    ('html/title_exist.html',               '',                 'Title for html (in title)'),
])
def test_title(gb_api, httpserver, filename, url, expected_title):
    httpserver.serve_content(content=open('data/' + filename, 'rb').read(),
                             headers={'content-type': mimetypes.guess_type(filename)[0]})

    # format url
    file_url = httpserver.url + '/' + url

    # add url
    assert gb_api.add_url(file_url) == True

    # verify result
    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1
    assert result['results'][0]['title'] == expected_title
