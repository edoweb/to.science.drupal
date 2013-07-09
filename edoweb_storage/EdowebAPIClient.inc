<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class EdowebAPIClient implements EdowebAPIClientInterface {

  /*
   * The URL of the Edoweb API
   */
  private $__edoweb_api_host;

  /**
   * The username for the Edoweb API
   *
   */
  private $__edoweb_api_user;

  /**
   * The password for the Edoweb API
   *
   */
  private $__edoweb_api_pass;

  public function __construct() {
    // Fetch credentials from drupal settings
    $this->__edoweb_api_host = variable_get('edoweb_api_host');
    $this->__edoweb_api_user = variable_get('edoweb_api_user');
    $this->__edoweb_api_pass = variable_get('edoweb_api_password');
  }

  /*
   * Load an entity from the API.
   *
   * @param $entity
   *   The drupal entity to load data into
   */
  public function load($entity) {
    // Fetch and add relations and files
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s/about',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_get($http_url);
    if (500 != $http_response->code) {
      $wrapper = entity_metadata_wrapper(EDOWEB_ENTITY_TYPE, $entity);
      $repository_data = json_decode($http_response->data);
      // Fetch and add children
      if (property_exists($repository_data, 'hasPart')) {
        if (!is_array($repository_data->hasPart)) {
          $children = array($repository_data->hasPart);
        } else {
          $children = $repository_data->hasPart;
        }
        foreach ($children as $remote_url) {
          $remote_id = substr($remote_url, strrpos($remote_url, '/') + 1);
          $local_id = db_query(
            'SELECT local_id FROM {edoweb_basic} WHERE remote_id = :remote_id',
            array(':remote_id' => $remote_id)
          )->fetchField();
          try {
            if ($wrapper->field_edoweb_child instanceOf EntityValueWrapper) {
              $wrapper->field_edoweb_child = $local_id;
            } else {
              $wrapper->field_edoweb_child[] = $local_id;
            }
          } catch (EntityMetadataWrapperException $e) {
            // TODO: log errors
            //drupal_set_message($e->getMessage(), 'error');
          }
        }
      }
      // Fetch and add parents
      if (property_exists($repository_data, 'isPartOf')) {
        if (!is_array($repository_data->isPartOf)) {
          $parents = array($repository_data->isPartOf);
        } else {
          $parents = $repository_data->isPartOf;
        }
        foreach ($parents as $remote_url) {
          $remote_id = substr($remote_url, strrpos($remote_url, '/') + 1);
          $local_id = db_query(
            'SELECT local_id FROM {edoweb_basic} WHERE remote_id = :remote_id',
            array(':remote_id' => $remote_id)
          )->fetchField();
          try {
            if ($wrapper->field_edoweb_parent instanceOf EntityValueWrapper) {
              $wrapper->field_edoweb_parent = $local_id;
            } else {
              $wrapper->field_edoweb_parent[] = $local_id;
            }
          } catch (EntityMetadataWrapperException $e) {
            // TODO: log errors
            //drupal_set_message($e->getMessage(), 'error');
          }
        }
      }
      // Fetch and add files.
      if (property_exists($repository_data, 'pdfUrl')) {
        $file_url = $repository_data->pdfUrl;
        $file_id = db_query(
          'SELECT fid FROM {file_managed} WHERE uri = :uri',
          array(':uri' => $file_url)
        )->fetchField();
        if ($file_id) {
          $file = file_load($file_id);
          try {
            $wrapper->field_edoweb_file[] = (array) $file;
          } catch (EntityMetadataWrapperException $e) {
            // TODO: log errors
            // drupal_set_message($e->getMessage(), 'error');
          }
        } else {
          // FIXME: why is the file not present?
        }
      }
    }

    // Fetch and add descriptive metadata
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_get($http_url);
    if (500 != $http_response->code) {
      $rdf_subject = _edoweb_storage_entity_get_uri($entity);
      $rdf_data = $http_response->data;
      $rdf_parser = new LibRDF_Parser('ntriples');
      _edoweb_storage_entity_deserialize_rdf($entity, $rdf_subject, $rdf_data, $rdf_parser);
    }
  }

  /*
   * Save an entity to the API
   *
   * @param $entity
   *   The drupal entity to store data from
   *
   */
  public function save($entity) {

    // Create resource
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $data = array(
      'type' => $entity->bundle_type,
    );
    $parents = field_get_items('edoweb_basic', $entity, 'field_edoweb_parent');
    if (FALSE !== $parents) {
      $local_id = $parents[0]['target_id'];
      $data['parentPid'] = _edoweb_storage_entity_get_remote_id($local_id);
    }
    $http_response = $this->_http_put($http_url, json_encode($data), 'application/json');

    // Store attached files first, as entity metadata has to be
    // updated with corresponding new file URIs
    $files = field_get_items('edoweb_basic', $entity, 'field_edoweb_file');
    if (FALSE !== $files) {
      foreach ($files as $file) {
        $file = file_load($file['fid']);
        $file_path = drupal_realpath($file->uri);
        $http_url = sprintf(
          'http://%s:%s@%s/resources/%s/data',
          $this->__edoweb_api_user,
          $this->__edoweb_api_pass,
          $this->__edoweb_api_host,
          $entity->remote_id
        );
        if ($this->_http_multipart_put($http_url, $file_path, $file->filemime)) {
          $file->uri = "http://{$this->__edoweb_api_host}/resources/{$entity->remote_id}/data";
          _edoweb_file_save($file, $entity);
          unlink($file_path);
        }
      }
    }

    // Send metadata
    $http_post_data = _edoweb_storage_entity_serialize_ntriples($entity);
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_post($http_url, $http_post_data);
  }

  /*
   * Delete an entity via the API
   *
   * @param $entity
   *   The drupal entity to delete
   *
   */
  public function delete($entity) {
    // Delete resource
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    //TODO: Delete children
    //$children = field_get_items('edoweb_basic', $entity, 'field_edoweb_child');
    //if (FALSE !== $children) {
    //  $local_id = $children[0]['target_id'];
    //}
    $http_response = $this->_http_delete($http_url);
  }

  /*
   * Import an entity based on its remote id.
   */
  public function importEntity($remote_id, $import_mode = 'overwrite', $metadata_required = FALSE) {

    // Check for metadata
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $remote_id
    );
    $http_response = $this->_http_get($http_url);
    if (404 == $http_response->code && 1 == $metadata_required) {
      return;
    }

    // Import the entity itself
    $http_url = sprintf(
      'http://%s:%s@%s/resources/%s/about',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $remote_id
    );
    $http_response = $this->_http_get($http_url);
    if (200 != $http_response->code) {
      return;
    }
    $repository_data = json_decode($http_response->data);
    $bundle_type = $repository_data->contentType;
    $entity = entity_get_controller('edoweb_basic')->create($bundle_type);
    if ('overwrite' == $import_mode) {
      $local_id = db_select('edoweb_basic')
        ->condition('remote_id', $remote_id, '=')
        ->fields('edoweb_basic', array('local_id'))
        ->execute()
        ->fetchField();
      $entity->local_id = $local_id;
    }
    $entity->created = time();
    $entity->remote_id = $remote_id;
    $primary_keys = $entity->local_id ? 'local_id' : array();

    // Directly write out the entity record bypassing the controller to
    // prevent needlessly writing objects back to the API
    try {
      drupal_write_record('edoweb_basic', $entity, $primary_keys);
      drupal_set_message("Imported $remote_id.");
    } catch (PDOException $e) {
      drupal_set_message("Failed to import $remote_id.");
      return;
    }

    // Fetch and add child relations.
    if (property_exists($repository_data, 'hasPart')) {
      if (!is_array($repository_data->hasPart)) {
        $children = array($repository_data->hasPart);
      } else {
        $children = $repository_data->hasPart;
      }
      foreach ($children as $remote_url) {
        $remote_id = substr($remote_url, strrpos($remote_url, '/') + 1);
        $this->importEntity($remote_id, $import_mode, $metadata_required);
      }
    }

    // Fetch and add files.
    _edoweb_entity_files_remove($entity);
    if (property_exists($repository_data, 'pdfUrl')) {
      $file_url = $repository_data->pdfUrl;
      $parsed_url = parse_url($file_url);
      $auth_file_url = sprintf(
        '%s://%s:%s@%s%s',
        $parsed_url['scheme'],
        $this->__edoweb_api_user,
        $this->__edoweb_api_pass,
        $parsed_url['host'],
        $parsed_url['path']
      );
      $file_metadata = $this->_http_head($auth_file_url);
      $file = new stdClass();
      $file->filemime = $file_metadata->headers['content-type'];
      $matches = array();
      $res = preg_match('/.*filename="(.*)"/', $file_metadata->headers['content-disposition'], $matches);
      if ($res and array_key_exists(1, $matches)) {
        $file->filename = $matches[1];
      } else {
        $file->filename = t('Unnamed file');
      }
      $file->uri = $file_url;
      _edoweb_file_save($file, $entity);
    }
  }

  /*
   * Import entities of a given bundle type.
   */
  public function import($bundle_type, $import_mode = 'overwrite', $metadata_required = FALSE) {
    // Build resource list url
    $http_url = sprintf(
      'http://%s:%s@%s/%s/',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $bundle_type
    );

    // Fetch response
    $http_response = $this->_http_get($http_url, 'application/json');

    // Wipe existing records if requested
    if ('clear' == $import_mode) {
      // Delete corresponding file records
      $entities = db_select('edoweb_basic', 'e')
        ->fields('e')
        ->condition('bundle_type', $bundle_type, '=')
        ->execute();
      foreach ($entities as $entity) {
        _edoweb_entity_files_remove($entity);
      }
      // Delete entity records
      $num_deleted = db_delete('edoweb_basic')
        ->condition('bundle_type', $bundle_type)
        ->execute();
      drupal_set_message("Deleted $num_deleted resources of type $bundle_type.");
    }

    // Process resource list
    $resource_list = json_decode($http_response->data)->list;
    foreach ($resource_list as $remote_id) {
      $this->importEntity($remote_id, $import_mode, $metadata_required);
    }
  }

  protected function _http_multipart_put($request_url, $file_path, $content_type = 'text/plain') {
    $data = array(
      'data' => "@$file_path",
      'type' => $content_type,
    );
    $ch = curl_init($request_url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PUT');
    curl_setopt($ch, CURLOPT_POSTFIELDS, $data);
    curl_setopt($ch, CURLOPT_HTTPHEADER, array('Content-type: multipart/mixed'));
    $response = curl_exec($ch);
    if (curl_errno($ch) && 1 == variable_get('edoweb_api_debug')) {
      drupal_set_message(curl_error($ch));
    } else {
      return TRUE;
    }
    return FALSE;
  }

  protected function _http_put($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'PUT',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_post($request_url, $request_body = '', $content_type='text/plain') {
    $http_options = array(
      'method' => 'POST',
      'data' => $request_body,
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_get($request_url, $content_type = 'text/plain') {
    $http_options = array(
      'method' => 'GET',
      'headers' => array('Content-Type' => $content_type),
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_delete($request_url) {
    $http_options = array(
      'method' => 'DELETE',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_head($request_url) {
    $http_options = array(
      'method' => 'HEAD',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

}

