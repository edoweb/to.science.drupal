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
          file_save($file);
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
    if (curl_errno($ch)) {
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

  protected function _http_get($request_url) {
    $http_options = array(
      'method' => 'GET',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

  protected function _http_delete($request_url) {
    $http_options = array(
      'method' => 'DELETE',
    );
    return _edoweb_http_request($request_url, $http_options);
  }

}

