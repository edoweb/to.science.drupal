# About

regal-drupal is a collection of Drupal 7 modules that provide a front
end for [regal](https://github.com/edoweb/regal) (repository and
graph-based api for library data).

# Installation

regal-drupal will create the user edoweb:edoweb2013. Make sure to
change the password as soon as installation is complete!

regal-drupal depends on the librdf and curl modules for php5.
Installation on Ubuntu, your distribution may vary:

    $ sudo apt-get install php5-librdf
    $ sudo apt-get install php5-curl

Clone the repository to Drupal's module directory:

    $ cd sites/all/modules
    $ git clone https://github.com/edoweb/regal-drupal.git

Clone libraries:

    $ cd sites/all/modules/regal-drupal/edoweb/lib
    $ git clone https://github.com/literarymachine/LibRDF.git
    $ git clone https://github.com/digitalbazaar/php-json-ld.git

Download non Drupal-core dependency modules:

    $ cd sites/all/modules
    $ curl http://ftp.drupal.org/files/projects/entity-7.x-1.1.tar.gz | tar xz
    $ curl http://ftp.drupal.org/files/projects/entityreference-7.x-1.0.tar.gz | tar xz
    $ curl http://ftp.drupal.org/files/projects/ctools-7.x-1.3.tar.gz | tar xz

Activate "Edoweb Entities" module at (e.g. at
<http://localhost/drupal/?q=admin/modules>) and confirm activation of
dependency modules. Finally, set the host, user and password for the API
at <http://localhost/drupal/?q=admin/config/edoweb/storage>.
