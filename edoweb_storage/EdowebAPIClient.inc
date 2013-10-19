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
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_get($http_url, 'application/rdf+xml');
    if ((200 == $http_response->code) && isset($http_response->data)) {
      $wrapper = entity_metadata_wrapper(EDOWEB_ENTITY_TYPE, $entity);
      $repository_data = new LibRDF_Model(new LibRDF_Storage());
      $repository_data->loadStatementsFromString(
        new LibRDF_Parser('rdfxml'), $http_response->data
      );
      // Fetch and add children
      $children = $repository_data->getTargets(
        new LibRDF_URINode($entity->remote_id),
        new LibRDF_URINode('http://purl.org/dc/terms/hasPart')
      );
      foreach ($children as $child) {
        $remote_id = substr($child, 1, -1);
        $local_id = db_query(
          'SELECT local_id FROM {edoweb_basic} WHERE remote_id = :remote_id',
          array(':remote_id' => $remote_id)
        )->fetchField();
        try {
          if ($wrapper->field_edoweb_struct_child instanceOf EntityValueWrapper) {
            $wrapper->field_edoweb_struct_child = $local_id;
          } else {
            $wrapper->field_edoweb_struct_child[] = $local_id;
          }
        } catch (EntityMetadataWrapperException $e) {
          // TODO: log errors
          //drupal_set_message($e->getMessage(), 'error');
        }
      }
      // Fetch and add parents
      $parents = $repository_data->getTargets(
        new LibRDF_URINode($entity->remote_id),
        new LibRDF_URINode('http://purl.org/dc/terms/isPartOf')
      );
      foreach ($parents as $parent) {
        $remote_id = substr($parent, 1, -1);
        $local_id = db_query(
          'SELECT local_id FROM {edoweb_basic} WHERE remote_id = :remote_id',
          array(':remote_id' => $remote_id)
        )->fetchField();
        try {
          if ($wrapper->field_edoweb_struct_parent instanceOf EntityValueWrapper) {
            $wrapper->field_edoweb_struct_parent = $local_id;
          } else {
            $wrapper->field_edoweb_struct_parent[] = $local_id;
          }
        } catch (EntityMetadataWrapperException $e) {
          // TODO: log errors
          //drupal_set_message($e->getMessage(), 'error');
        }
      }
      // FIXME: hardcoded URL for datastreams
      // TODO: decide on explicit rdf property for datastream links
      if (_edoweb_field_access($entity, array('field_edoweb_datastream'))) {
        $wrapper->field_edoweb_datastream =
          "http://{$this->__edoweb_api_host}/resource/{$entity->remote_id}/data";
      }
      // Load remaining (descriptive) metadata into fields
      $rdf_subject = _edoweb_storage_entity_get_uri($entity);
      $rdf_data = $http_response->data;
      $rdf_parser = new LibRDF_Parser('rdfxml');
      _edoweb_storage_entity_deserialize_rdf($entity, $rdf_subject, $rdf_data, $rdf_parser);
    } else {
      watchdog('edoweb_storage', 'Unable to load data for @id from @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
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
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    $data = array(
      'type' => $entity->bundle_type,
    );
    $parents = field_get_items('edoweb_basic', $entity, 'field_edoweb_struct_parent');
    if (FALSE !== $parents) {
      $local_id = $parents[0]['target_id'];
      $data['parentPid'] = _edoweb_storage_entity_get_remote_id($local_id);
    }
    $http_response = $this->_http_put($http_url, json_encode($data), 'application/json');
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to save @id to @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }

    // Store attached files first, as entity metadata has to be
    // updated with corresponding new file URIs
    if (property_exists($entity, 'field_edoweb_datastream')) {
      $file_url = $entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'];
    }
    if (isset($file_url) && 'public://' == substr($file_url, 0, 9)) {
      $file_name = $entity->field_edoweb_label[LANGUAGE_NONE][0]['value'];
      $file_mime = $entity->field_edoweb_filetype[LANGUAGE_NONE][0]['value'];
      $file_path = drupal_realpath($file_url);
      if ($this->_http_multipart_put("$http_url/data", $file_path, $file_mime, $file_name)) {
        $entity->field_edoweb_datastream[LANGUAGE_NONE][0]['value'] =
          "http://{$this->__edoweb_api_host}/resource/{$entity->remote_id}/data";
        unlink($file_path);
        cache_clear_all("field:edoweb_basic:{$entity->local_id}", 'cache_field');
      } else {
        watchdog('edoweb_storage', 'Unable to upload file to API');
      }
    }

    // Send metadata
    $http_post_data = _edoweb_storage_entity_serialize_ntriples($entity);
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );
    $http_response = $this->_http_post($http_url, $http_post_data);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to save metadata for @id to @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }
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
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $entity->remote_id
    );

    //TODO: Delete children, currently handeled by Edoweb API
    //$children = field_get_items('edoweb_basic', $entity, 'field_edoweb_struct_child');
    //if (FALSE !== $children) {
    //  $local_id = $children[0]['target_id'];
    //}
    $http_response = $this->_http_delete($http_url);
    if (200 != $http_response->code) {
      watchdog('edoweb_storage', 'Unable to delete @id from @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }
  }

  /*
   * Import an entity based on its remote id.
   */
  public function importEntity($remote_id, $import_mode = 'overwrite', $metadata_required = FALSE) {

    // Check for metadata
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s/metadata',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $remote_id
    );
    $http_response = $this->_http_get($http_url);
    if (404 == $http_response->code && 1 == $metadata_required) {
      watchdog('edoweb_storage', 'Unable to import required metadata for @id from @api.',
        array(
          '@id' => $entity->remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }

    // Import the entity itself
    $http_url = sprintf(
      'http://%s:%s@%s/resource/%s',
      $this->__edoweb_api_user,
      $this->__edoweb_api_pass,
      $this->__edoweb_api_host,
      $remote_id
    );
    $http_response = $this->_http_get($http_url, 'application/rdf+xml');
    if (200 != $http_response->code || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to import @id from @api.',
        array(
          '@id' => $remote_id,
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }
    $repository_data = new LibRDF_Model(new LibRDF_Storage());
    $repository_data->loadStatementsFromString(
      new LibRDF_Parser('rdfxml'), $http_response->data
    );
    LibRDF_LiteralNode::setPlainOutput(true);
    $bundle_type = (string) $repository_data->getTarget(
      new LibRDF_URINode($remote_id),
      new LibRDF_URINode('http://hbz-nrw.de/regal#contentType')
    );
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

    // TODO: Load entity so that it is cached.
    // $this->load($entity);

    // Fetch and add child relations.
    $children = $repository_data->getTargets(
      new LibRDF_URINode($entity->remote_id),
      new LibRDF_URINode('http://purl.org/dc/terms/hasPart')
    );
    foreach ($children as $child) {
      $remote_id = substr($child, 1, -1);
      $this->importEntity($remote_id, $import_mode, $metadata_required);
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

    // Fetch resource list
    $http_response = $this->_http_get($http_url, 'application/json');
    if ((200 != $http_response->code) || !isset($http_response->data)) {
      watchdog('edoweb_storage', 'Unable to fetch resource listing from @api.',
        array(
          '@api' => variable_get('edoweb_api_host'),
        )
      );
      return;
    }

    // Wipe existing records if requested
    if ('clear' == $import_mode) {
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

  protected function _http_multipart_put($request_url, $file_path, $content_type = null, $file_name = null) {
    $data = array(
      'data' => "@$file_path",
      'type' => $content_type,
      'label' => $file_name,
    );
    $ch = curl_init($request_url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PUT');
    curl_setopt($ch, CURLOPT_POSTFIELDS, $data);
    curl_setopt($ch, CURLOPT_HTTPHEADER, array('Content-type: multipart/mixed'));
    $response = curl_exec($ch);
    if (curl_errno($ch) && 1 == variable_get('edoweb_api_debug')) {
      drupal_set_message(curl_error($ch));
      return FALSE;
    } else {
      return TRUE;
    }
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
      'headers' => array('Accept' => $content_type),
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

