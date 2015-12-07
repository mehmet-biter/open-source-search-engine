import pytest
import mimetypes


def verify_file(gb_api, httpserver, filename, custom_filename, content_type, expected_content_type):
    if not content_type:
        # guess content type
        content_type = mimetypes.guess_type(filename)[0]

    httpserver.serve_content(content=open(filename, 'rb').read(),
                             headers={'content-type': content_type})

    # format url
    file_url = httpserver.url + '/' + custom_filename

    # add url
    assert gb_api.add_url(file_url) == True

    result = gb_api.search('url:' + file_url)
    assert len(result['results']) == 1

    assert result['results'][0]['contentType'] == expected_content_type


@pytest.mark.parametrize('filename, custom_filename, content_type, expected_content_type', [
    ('src/example_cpp.cpp',     'example_cpp.cpp',      'text/x-c++src',     'text'),
    ('src/example_cpp.cpp',     'example_plain.cpp',    'text/plain',        'text'),
    ('src/example_cpp.cpp',     'example_audio.cpp',    'audio/3gpp',        ''),
])
def test_file_cpp(gb_api, httpserver, filename, custom_filename, content_type, expected_content_type):
    verify_file(gb_api, httpserver, 'data/' + filename, custom_filename, content_type, expected_content_type)
